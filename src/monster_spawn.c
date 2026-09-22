/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* monster_spawn.c - the COLD third of the monster module: level-entry spawning
 * and the killed-monster persistence/save. Split out of monster_ai.c (v0.11)
 * because monster_ai's bank filled to the brim and the per-turn AI needed its
 * room back. Everything here runs once per level entry (or once per save), so
 * it banks freely into whichever bank has room (banks.json).
 *
 * The monster arrays and per-cell lookups stay RESIDENT in monster.c (reached
 * by direct calls); mon_dead is shared with monster_ai.c's combat (a kill sets
 * its bit there), so it is defined in monster.c and extern'd in monster.h. */

#include "monster.h"
#include "level.h"        /* lvl, rand_floor, rcount, up_x/up_y, eff_depth    */
#include "platform.h"     /* file_read/file_write                             */
#include "rng.h"          /* rn2                                              */
#include "game.h"         /* dlvl, has_amulet, hero_x/hero_y                  */

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

/* pick_mon, minus the genocided. The filter lives HERE, banked, rather than
 * in the resident pick_mon: three expansions of the mask test there cost the
 * Next 150 B of a resident half that had 285. Every random spawn -- the
 * level's, the vault guards', the wanderers' -- is drawn in this file, so this
 * one wrapper covers them all. A genocided draw is re-rolled (the extra rolls
 * happen only after a genocide, so the spawn stream is otherwise the old one);
 * 0 means eight draws all came back genocided, and the caller spawns nothing
 * rather than resurrect the type. */
static char pick_living(void)
{
    uint8_t t;
    for (t = 0; t < 8; t++) {
        char c = pick_mon();
        if (!mon_gone(c)) return c;
    }
    return 0;
}

static void spawn_monster(char type)
{
    const MonType *mt = mon_find(type);
    uint8_t i, x, y;
    if (!type || mcount >= MAXMON) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;                 /* floor only          */
    if (x == up_x && y == up_y) return;           /* keep the start clear */
    if (shop_in_room(x, y)) return;               /* shops hold only the keeper */
    if (monster_at(x, y) >= 0) return;
    m_x[mcount] = x; m_y[mcount] = y;
    m_hp[mcount] = (uint8_t)(mt->hp + eff_depth() / 2);   /* tougher when deep */
    m_type[mcount] = type; m_alive[mcount] = 1;
    if (dlvl == (uint16_t)(MINES_BASE + 1) && (type == 'G' || type == 'h')) {
        /* Minetown's natives go about their business: peaceful (and awake --
         * the town is busy) until the hero draws blood; hit_monster then
         * angers the whole town. (Skipping the sleep hash is safe: it is
         * pure, so the spawn stream is unchanged.) */
        m_peace[mcount] = 1;
    } else if (type != 'x' && spawns_asleep(x, y)) {
        /* Half the dungeon sleeps until you disturb it, as in NetHack. 255
         * is the "until woken" sentinel (see monster_ai's still_asleep),
         * not a timer. Hidden mimics stay "awake": their pose already is
         * the ambush. Guards, the guardian and wanderers spawn alert. */
        m_sleep[mcount] = 255;
    }
    mcount++;
}

/* A vault guard: a tough monster (the tougher of two depth-appropriate draws),
 * with a bigger HP bonus than usual, placed inside the treasure vault.
 * rand_floor returns an interior cell (floor or treasure -- the guard just
 * stands on top), so no '.'-only check. */
static void spawn_guard(uint8_t room)
{
    char a = pick_living(), b = pick_living();
    const MonType *mt;
    uint8_t x, y;
    if (!a) a = b;                      /* one genocided draw: take the other */
    if (!b) b = a;
    if (!a || mcount >= MAXMON) return;
    mt = mon_find(mon_find(a)->hp >= mon_find(b)->hp ? a : b);
    rand_floor(room, &x, &y);
    if (x == up_x && y == up_y) return;
    if (monster_at(x, y) >= 0) return;
    m_x[mcount] = x; m_y[mcount] = y;
    /* eff_depth, like every other difficulty number -- raw dlvl would give a
     * mines vault guard 51+ HP. There are no vaults in the mines today, so
     * this changes nothing; it stops the next person finding out. */
    m_hp[mcount] = (uint8_t)(mt->hp + eff_depth());  /* tougher than the usual half */
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
    { uint8_t k;
      for (k = 0; k < MAXMON; k++) { m_sleep[k] = 0; m_peace[k] = 0; m_face[k] = 0; } }
    if (dlvl == DLVL_AMULET) {          /* the Amulet's keeper takes slot 0 */
        spawn_guardian();
        if (count > 7) count = 7;       /* randoms stay in tracked slots 1-7 */
    }
    for (i = 0; i < count; i++) {
        if (i < guards) spawn_guard((uint8_t)vr);   /* low slots -> persistence-tracked */
        else            spawn_monster(pick_living());
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
    for (i = 0; i < sizeof mon_geno; i++)
        mon_geno[i] = 0;
}

void monster_save(uint8_t h) __banked
{
    file_write(h, mon_dead, MAXLVL + 1);
    file_write(h, mon_geno, sizeof mon_geno);
}

void monster_load(uint8_t h) __banked
{
    file_read(h, mon_dead, MAXLVL + 1);
    file_read(h, mon_geno, sizeof mon_geno);
}

/* Reverse genocide (a cursed scroll of genocide): a pack of `type` in the
 * free cells around the hero, awake and hostile, as many as the cells and
 * the monster slots allow -- NetHack sends four to six. A genocided type
 * sends none. Dead slots are reused the way the wanderer reuses them.
 * Returns how many came. */
uint8_t summon_near(char type) __banked
{
    const MonType *mt = mon_find(type);
    uint8_t n = 0, want = (uint8_t)(4 + rn2(3)), slot, i;
    int dx, dy;
    if (mon_gone(type)) return 0;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int x = hero_x + dx, y = hero_y + dy;
            if (n >= want) return n;
            if (dx == 0 && dy == 0) continue;
            if (!walkable(terrain(x, y)) || lvl[y][x] == '+' ||
                monster_at(x, y) >= 0) continue;
            slot = MAXMON;
            for (i = 0; i < mcount; i++)
                if (!m_alive[i] && (int8_t)i != pet_idx) { slot = i; break; }
            if (slot == MAXMON) {
                if (mcount >= MAXMON) return n;
                slot = mcount;
            }
            m_x[slot]     = (uint8_t)x;
            m_y[slot]     = (uint8_t)y;
            m_hp[slot]    = (uint8_t)(mt->hp + eff_depth() / 2);
            m_type[slot]  = type;
            m_alive[slot] = 1;
            m_sleep[slot] = 1;          /* they act from the NEXT turn: the
                                         * message line has one line, and a
                                         * horde biting at once overwrote the
                                         * news of its own arrival */
            m_peace[slot] = 0;
            m_face[slot]  = 0;
            if (slot == mcount) mcount++;
            n++;
        }
    return n;
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

    type = pick_living();
    if (!type) return;                      /* everything drawn is genocided */
    mt   = mon_find(type);

    i = (uint8_t)rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.')       return;      /* floor only            */
    if (x == up_x && y == up_y) return;      /* keep the start clear  */
    if (shop_in_room(x, y))     return;      /* shops hold only the keeper */
    if (monster_at(x, y) >= 0)  return;      /* not onto another mon  */
    if (iabs((int)x - hero_x) <= 1 &&
        iabs((int)y - hero_y) <= 1) return;  /* not in the hero's lap */
    if (fov_visible(x, y))      return;      /* and never in plain sight: a
                                              * monster popping into existence
                                              * across a lit room reads as a
                                              * rendering ghost (the header
                                              * always promised off-screen) */

    m_x[slot]    = x;
    m_y[slot]    = y;
    m_hp[slot]   = (uint8_t)(mt->hp + eff_depth() / 2);
    m_type[slot] = type;
    m_alive[slot] = 1;
    m_sleep[slot] = 0;          /* a fresh wanderer is awake */
    m_face[slot]  = 0;          /* (a reused dead slot may hold a stale pose) */
    m_peace[slot] = (uint8_t)(dlvl == (uint16_t)(MINES_BASE + 1) &&
                              (type == 'G' || type == 'h'));  /* townsfolk */
    if (slot == mcount) mcount++;
}
