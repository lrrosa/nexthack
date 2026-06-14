/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* monster_ai.c - BANKED half of the monster module: depth-based spawning,
 * the per-turn BFS chase, combat and experience, and save/restore of the
 * killed-monster bitmask. Split out of monster.c so this cold-ish code lives
 * in PAGE_20_CODE (mapped into the 0xC000 window on demand).
 *
 * The monster arrays and the per-cell lookups (monster_at/mon_find/pick_mon)
 * stay RESIDENT in monster.c; this file reaches them by direct (resident)
 * calls. Its own entry points are __banked (see monster.h). */

#include "monster.h"
#include "level.h"        /* lvl, terrain, rand_floor, rcount, up_x/up_y      */
#include "platform.h"     /* msg/msg2, file_read/file_write                   */
#include "rng.h"          /* rn2                                              */
#include "game.h"         /* hero/php/dead/dlvl, xp/xlvl, weapon_dmg/armor_def */
#include "sfx.h"          /* sound effects                                    */
#include "item.h"         /* corrode_worn                                     */

#pragma codeseg PAGE_20_CODE

static uint8_t mon_dead[MAXLVL + 1];     /* bit i: monster i killed */

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
    m_hp[mcount] = (uint8_t)(mt->hp + dlvl / 2);   /* tougher the deeper you go */
    m_type[mcount] = type; m_alive[mcount] = 1;
    mcount++;
}

void spawn_level_monsters(void) __banked
{
    uint8_t count = (uint8_t)(2 + dlvl);   /* more monsters the deeper you go */
    uint8_t i;
    if (count > MAXMON) count = MAXMON;
    mcount = 0;
    for (i = 0; i < count; i++)
        spawn_monster(pick_mon());
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

/* ---- experience ---- */
static void gain_xp(uint8_t amt)
{
    xp = (uint16_t)(xp + amt);
    while (xlvl < 30 && xp >= (uint16_t)xlvl * 20) {
        uint8_t gain = (uint8_t)(rn2(4) + 2);   /* 2..5 max-HP boost */
        xlvl++;
        pmaxhp = (uint8_t)(pmaxhp + gain);
        php = (uint8_t)(php + gain);
        msg("Welcome to a new experience level!");
        sfx_levelup();
    }
}

void attack_monster(uint8_t mi) __banked
{
    const MonType *mt = mon_find(m_type[mi]);
    uint8_t dmg = (uint8_t)(rn2(4) + 1 + weapon_dmg);  /* 1..4 + weapon */

    turns++;
    if (m_hp[mi] <= dmg) {
        m_alive[mi] = 0;
        if (dlvl <= MAXLVL)
            mon_dead[dlvl] |= (uint8_t)(1u << mi);   /* remember the kill */
        msg2("You kill the ", mt->name, "!");
        sfx_kill();
        gain_xp(mt->xp);
    } else {
        m_hp[mi] = (uint8_t)(m_hp[mi] - dmg);
        msg2("You hit the ", mt->name, ".");
        sfx_hit();
    }
    if (mt->corr && rn2(2))         /* acid eats the weapon you strike it with */
        corrode_worn(')');
}

/* ---- monster turn: chase the hero, attack when adjacent ---- */

static int iabs(int v) { return v < 0 ? -v : v; }

static void monster_hits_player(uint8_t i)
{
    const MonType *mt = mon_find(m_type[i]);
    uint8_t bite = (uint8_t)(rn2(mt->dmg) + 1 + dlvl / 4);  /* harder when deep */

    if (armor_def >= bite) {                 /* armor soaks the blow */
        msg2("The ", mt->name, " misses you!");
        return;
    }
    bite = (uint8_t)(bite - armor_def);
    sfx_hurt();

    if (php <= bite) {
        php = 0; dead = 1;
        msg2("The ", mt->name, " kills you!");
    } else {
        php = (uint8_t)(php - bite);
        msg2("The ", mt->name, " bites you!");
        if (mt->corr && rn2(2))     /* acid/rust corrodes your worn armour */
            corrode_worn('[');
    }
}

/* ---- pathfinding: a BFS distance field from the hero ("Dijkstra map").
 * Computed once per turn; each monster then steps to the neighbouring cell
 * with the smallest distance, which routes optimally around walls.        */

#define UNREACH 255
/* BFS frontier queue. A level has far fewer walkable cells than MAPW*MAPH, so
 * a bounded queue saves RAM; enqueues are guarded so it can never overflow.
 * 696 (was 700) so it packs exactly behind dist[] in Bank 5: dist is 1680 B at
 * 0x7400, bfsq is 696*2=1392 B at 0x7A90, ending exactly at 0x8000. */
#define BFSQ_SIZE 696

/* dist[] lives in Bank 5's free space (after the 80x32x2 tilemap, 0x7400),
 * which the CPU always sees at 0x4000-0x7FFF (segment 1) and which code banking
 * (segment 3, 0xC000) never touches. Placing this 1680-byte per-turn scratch
 * map there frees that much of the tight resident BSS budget. Safe because it
 * is rewritten every turn and never read during esxDOS file I/O. NOTE: 0x7400
 * assumes the tilemap ends there (TILEMAP_BASE 0x6000 + 80*32*2) -- keep in
 * sync with platform.c if the tilemap geometry changes.
 * SDCC rejects casts to a pointer-to-array type, so index the flat view as
 * dist[y*MAPW + x] (compute_dist_map works through the flat `d`). */
#define dist ((uint8_t *)0x7400u)
/* bfsq also lives in Bank 5 (right after dist), same rationale: pure per-turn
 * BFS scratch, never touched during esxDOS file I/O. */
#define bfsq ((uint16_t *)0x7A90u)

static void compute_dist_map(void)
{
    uint16_t head = 0, tail = 0, k;
    uint8_t *d = (uint8_t *)dist;      /* flat view, fast indexing */
    const char *lf = (const char *)lvl;

    for (k = 0; k < (uint16_t)(MAPH * MAPW); k++)
        d[k] = UNREACH;

    if (hero_x < 0 || hero_y < 0 || hero_x >= MAPW || hero_y >= MAPH)
        return;

    /* queue entries are packed as (y << 8) | x to avoid div/mod on dequeue */
    d[(uint16_t)hero_y * MAPW + hero_x] = 0;
    bfsq[tail++] = (uint16_t)(((uint16_t)hero_y << 8) | (uint8_t)hero_x);

    while (head < tail) {
        uint16_t p     = bfsq[head++];
        uint8_t  cx    = (uint8_t)(p & 0xFF);
        uint8_t  cy    = (uint8_t)(p >> 8);
        uint16_t cbase = (uint16_t)cy * MAPW;
        uint8_t  nd    = (uint8_t)(d[cbase + cx] + 1);
        int dx, dy;
        for (dy = -1; dy <= 1; dy++) {
            int ny = (int)cy + dy;
            uint16_t rbase;
            if (ny < 0 || ny >= MAPH) continue;
            rbase = (uint16_t)((int)cbase + dy * MAPW);
            for (dx = -1; dx <= 1; dx++) {
                int nx;
                uint16_t np;
                char c;
                if (dx == 0 && dy == 0) continue;
                nx = (int)cx + dx;
                if (nx < 0 || nx >= MAPW) continue;
                np = (uint16_t)(rbase + nx);
                if (d[np] != UNREACH) continue;
                c = lf[np];                       /* inline walkable check */
                if (c == '|' || c == '-' || c == ' ') continue;
                d[np] = nd;
                if (tail < BFSQ_SIZE)
                    bfsq[tail++] = (uint16_t)(((uint16_t)ny << 8) | (uint8_t)nx);
            }
        }
    }
}

/* a monster steps to the neighbour closest to the hero (lowest distance) */
static void mon_step(uint8_t i)
{
    int ddx, ddy;

    if (m_type[i] == MON_KEEPER) return;   /* the shopkeeper never moves */

    ddx = hero_x - (int)m_x[i];
    ddy = hero_y - (int)m_y[i];
    uint8_t bestd;
    int bestx = -1, besty = -1, dx, dy;

    if (iabs(ddx) <= 1 && iabs(ddy) <= 1) {   /* adjacent -> attack */
        monster_hits_player(i);
        return;
    }

    bestd = dist[(uint16_t)m_y[i] * MAPW + m_x[i]];   /* our current distance */
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = (int)m_x[i] + dx, ny = (int)m_y[i] + dy;
            uint8_t nd;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            nd = dist[(uint16_t)ny * MAPW + nx];
            if (nd == UNREACH || nd >= bestd) continue;
            if (monster_at(nx, ny) >= 0) continue;   /* don't stack */
            bestd = nd; bestx = nx; besty = ny;
        }
    }
    if (bestx >= 0) { m_x[i] = (uint8_t)bestx; m_y[i] = (uint8_t)besty; }
}

void monsters_turn(void) __banked
{
    uint8_t i;
    compute_dist_map();
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i]) continue;
        mon_step(i);
        if (dead) return;
    }
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
    m_hp[slot]   = (uint8_t)(mt->hp + dlvl / 2);
    m_type[slot] = type;
    m_alive[slot] = 1;
    if (slot == mcount) mcount++;
}
