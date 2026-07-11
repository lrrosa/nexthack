/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* monster_spawn.c - the COLD third of the monster module: level-entry spawning
 * and the killed-monster persistence/save. Split out of monster_ai.c (v0.11)
 * because that bank (PAGE_26 / the 128K's BANK_6) filled to the brim and the
 * per-turn AI needed its room back. Everything here runs once per level entry
 * (or once per save), so it banks freely into the roomier PAGE_20 / BANK_1.
 *
 * The monster arrays and per-cell lookups stay RESIDENT in monster.c (reached
 * by direct calls); mon_dead is shared with monster_ai.c's combat (a kill sets
 * its bit there), so it is defined in monster.c and extern'd in monster.h. */

#include "monster.h"
#include "level.h"        /* lvl, rand_floor, rcount, up_x/up_y, eff_depth    */
#include "platform.h"     /* file_read/file_write                             */
#include "rng.h"          /* rn2                                              */
#include "game.h"         /* dlvl, has_amulet, hero_x/hero_y                  */

#ifdef __ZXNEXT
#pragma codeseg PAGE_20_CODE
#pragma constseg PAGE_20_CODE
#else
#pragma codeseg BANK_1
#pragma constseg BANK_1
#endif

static int iabs(int v) { return v < 0 ? -v : v; }

/* Does the monster spawning at (x,y) start asleep? A pure side hash (the
 * item_hash pattern), never rn2: an extra roll here would shift the spawn
 * stream and desync the mon_dead slot indices under the frozen save format.
 * Deterministic per (seed, depth, cell), so a revisited level sleeps the
 * same way it did the first time. */
static uint8_t spawns_asleep(uint8_t x, uint8_t y)
{
    uint16_t h = (uint16_t)(world_seed + (uint16_t)dlvl * 4241u
                            + (uint16_t)x * 269u + (uint16_t)y * 733u);
    h ^= (uint16_t)(h << 7);
    h ^= (uint16_t)(h >> 9);
    h ^= (uint16_t)(h << 8);
    return (uint8_t)(h & 1);
}

static void spawn_monster(char type)
{
    const MonType *mt = mon_find(type);
    uint8_t i, x, y;
    if (mcount >= MAXMON) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;                 /* floor only          */
    if (x == up_x && y == up_y) return;           /* keep the start clear */
    if (shop_in_room(x, y)) return;               /* shops hold only the keeper */
    if (monster_at(x, y) >= 0) return;
    m_x[mcount] = x; m_y[mcount] = y;
    m_hp[mcount] = (uint8_t)(mt->hp + eff_depth() / 2);   /* tougher when deep */
    m_type[mcount] = type; m_alive[mcount] = 1;
    /* Half the dungeon sleeps until you disturb it, as in NetHack. 255 is
     * the "until woken" sentinel (see monster_ai's still_asleep), not a
     * timer. Hidden mimics stay "awake": their pose already is the ambush.
     * Vault guards, the guardian and wanderers spawn alert elsewhere. */
    if (type != 'x' && spawns_asleep(x, y))
        m_sleep[mcount] = 255;
    mcount++;
}

/* A vault guard: a tough monster (the tougher of two depth-appropriate draws),
 * with a bigger HP bonus than usual, placed inside the treasure vault.
 * rand_floor returns an interior cell (floor or treasure -- the guard just
 * stands on top), so no '.'-only check. */
static void spawn_guard(uint8_t room)
{
    char a = pick_mon(), b = pick_mon();
    const MonType *mt = mon_find(mon_find(a)->hp >= mon_find(b)->hp ? a : b);
    uint8_t x, y;
    if (mcount >= MAXMON) return;
    rand_floor(room, &x, &y);
    if (x == up_x && y == up_y) return;
    if (monster_at(x, y) >= 0) return;
    m_x[mcount] = x; m_y[mcount] = y;
    m_hp[mcount] = (uint8_t)(mt->hp + dlvl);       /* tougher than the usual +dlvl/2 */
    m_type[mcount] = mt->ch; m_alive[mcount] = 1;
    mcount++;
}

/* The Amulet's keeper: a lone high priest posted ON the Amulet's cell (the
 * would-be down-stairs of DLVL_AMULET, chosen without RNG, so the level's
 * deterministic spawns are untouched). Slot 0, so the mon_dead bitmask
 * remembers the kill: slay it once and the Sanctum stays yours. */
static void spawn_guardian(void)
{
    const MonType *mt = mon_find('M');
    m_x[mcount] = dn_x; m_y[mcount] = dn_y;
    m_hp[mcount] = mt->hp;              /* no depth bonus: already the apex */
    m_type[mcount] = 'M';
    m_alive[mcount] = 1;
    mcount++;
}

void spawn_level_monsters(void) __banked
{
    uint8_t count = (uint8_t)(2 + eff_depth());   /* more monsters when deep */
    int     vr    = level_vault_room();    /* -1 if this level has no vault     */
    uint8_t guards = (vr >= 0) ? 3 : 0;    /* a few tough guards inside it       */
    uint8_t i;
    /* Keep every random mob in slots 0..7, which the uint8_t mon_dead kill-
     * bitmask can track; the two slots above (MAXMON=10) are reserved for the
     * shopkeeper and the pet, which are never persistence-tracked, so even a
     * crowded shop level always has room for the dog. */
    if (count > 8) count = 8;
    if (guards > count) guards = count;
    mcount = 0;
    { uint8_t k; for (k = 0; k < MAXMON; k++) m_sleep[k] = 0; }   /* none asleep yet */
    if (dlvl == DLVL_AMULET) {          /* the Amulet's keeper takes slot 0 */
        spawn_guardian();
        if (count > 7) count = 7;       /* randoms stay in tracked slots 1-7 */
    }
    for (i = 0; i < count; i++) {
        if (i < guards) spawn_guard((uint8_t)vr);   /* low slots -> persistence-tracked */
        else            spawn_monster(pick_mon());
    }
}

/* Append the shopkeeper at (x,y). Called from build_level AFTER the random
 * monsters (which reset mcount), so the keeper gets a stable high slot that the
 * deterministic mob spawns never reuse. */
void place_shopkeeper(uint8_t x, uint8_t y) __banked
{
    const MonType *mt = mon_find(MON_KEEPER);
    if (mcount >= MAXMON) return;
    if (monster_at(x, y) >= 0) return;
    m_x[mcount]     = x;
    m_y[mcount]     = y;
    m_hp[mcount]    = mt->hp;
    m_type[mcount]  = MON_KEEPER;
    m_alive[mcount] = 1;
    mcount++;
}

void apply_monster_persistence(void) __banked
{
    uint8_t b;
    if (dlvl > MAXLVL) return;
    for (b = 0; b < mcount; b++)
        if (mon_dead[dlvl] & (uint8_t)(1u << b))
            m_alive[b] = 0;
}

void monster_reset_persistence(void) __banked
{
    uint8_t i;
    for (i = 0; i <= MAXLVL; i++)
        mon_dead[i] = 0;
}

void monster_save(uint8_t h) __banked
{
    file_write(h, mon_dead, MAXLVL + 1);
}

void monster_load(uint8_t h) __banked
{
    file_read(h, mon_dead, MAXLVL + 1);
}

/* ---- wandering monsters ----
 * NetHack keeps generating monsters over time, so a level is never permanently
 * cleared by camping.  Each turn there is a small chance (~1/70, faster while
 * carrying the Amulet) to add one.  A freed (dead) slot is reused when one is
 * available, else a new slot is appended up to MAXMON; the newcomer always
 * arrives off-screen (never in the hero's lap).  Wanderers are not persisted:
 * they share the mon_dead bitmask space but live only on the current visit.
 *
 * This rolls rn2(), so it must run only from the turn loop -- never inside
 * gen_level(), which reseeds the RNG per depth; an extra roll there would
 * desync the deterministic generation and the persistence bit indices. */
void maybe_spawn_wanderer(void) __banked
{
    const MonType *mt;
    char    type;
    uint8_t slot, i, x, y;

    if (rn2(has_amulet ? 25 : 70) != 0) return;

    slot = MAXMON;                          /* find a reusable dead slot...   */
    for (i = 0; i < mcount; i++)
        if (!m_alive[i]) { slot = i; break; }
    if (slot == MAXMON) {                    /* ...else append if there's room */
        if (mcount >= MAXMON) return;
        slot = mcount;
    }

    type = pick_mon();
    mt   = mon_find(type);

    i = (uint8_t)rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.')       return;      /* floor only            */
    if (x == up_x && y == up_y) return;      /* keep the start clear  */
    if (shop_in_room(x, y))     return;      /* shops hold only the keeper */
    if (monster_at(x, y) >= 0)  return;      /* not onto another mon  */
    if (iabs((int)x - hero_x) <= 1 &&
        iabs((int)y - hero_y) <= 1) return;  /* not in the hero's lap */

    m_x[slot]    = x;
    m_y[slot]    = y;
    m_hp[slot]   = (uint8_t)(mt->hp + eff_depth() / 2);
    m_type[slot] = type;
    m_alive[slot] = 1;
    m_sleep[slot] = 0;          /* a fresh wanderer is awake */
    if (slot == mcount) mcount++;
}
