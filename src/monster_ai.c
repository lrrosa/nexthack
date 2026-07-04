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

#ifdef __ZXNEXT
#pragma codeseg PAGE_26_CODE   /* bank 13: a third code bank (PAGE_20 filled up) */
#pragma constseg PAGE_26_CODE
#else
#pragma codeseg BANK_6   /* the spare 128K bank: the pet AI outgrew BANK_1 */
#pragma constseg BANK_6
#endif
/* constseg: this file's message literals live in its own bank (consumed by
 * msg/msg2 while it is mapped), not the tight resident half. Don't pass them
 * into another bank's __banked functions. */

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

void spawn_level_monsters(void) __banked
{
    uint8_t count = (uint8_t)(2 + dlvl);   /* more monsters the deeper you go */
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

/* place_pet lives in nexthack.c (PAGE_22) to keep this bank under its 16 KB. */

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
        uint8_t gain = (uint8_t)(rn2(4) + 2 +
                                 (at_con >= 14 ? 1 : 0)); /* 2..5 max HP,
                                                           * +1 if hardy */
        xlvl++;
        pmaxhp = (uint8_t)(pmaxhp + gain);
        php = (uint8_t)(php + gain);
        msg("Welcome to a new level!");
        sfx_levelup();
    }
}

/* Apply dmg to monster mi: kill it (with XP) or wound it, with the matching
 * "You kill/hit the X" message and sound. Shared by melee (attack_monster) and
 * thrown weapons (item.c do_throw). Does not consume a turn -- the caller does. */
void hit_monster(uint8_t mi, uint8_t dmg) __banked
{
    const MonType *mt = mon_find(m_type[mi]);
    if (m_hp[mi] <= dmg) {
        m_alive[mi] = 0;
        if (dlvl <= MAXLVL)
            mon_dead[dlvl] |= (uint8_t)(1u << mi);   /* remember the kill */
        if (m_type[mi] != MON_KEEPER && rn2(2))      /* it may leave a corpse */
            corpse_drop(m_x[mi], m_y[mi], m_type[mi]);
        msg2("You kill the ", mt->name, "!");
        sfx_kill();
        gain_xp(mt->xp);
    } else {
        m_hp[mi] = (uint8_t)(m_hp[mi] - dmg);
        msg2("You hit the ", mt->name, ".");
        sfx_hit();
    }
}

void attack_monster(uint8_t mi) __banked
{
    const MonType *mt = mon_find(m_type[mi]);
    uint8_t dmg;

    turns++;
    /* Dexterity decides whether the swing lands at all (Dx 11 = 85%, 16+ =
     * always) -- the whiffed turn still passes, as in NetHack. */
    if (rn2(20) >= (uint8_t)(12 + (at_dex >> 1))) {
        msg2("You miss the ", mon_name(m_type[mi]), ".");
        return;
    }
    dmg = (uint8_t)(rn2(4) + 1 + weapon_dmg);   /* 1..4 + weapon */
    if (at_str >= 17)      dmg = (uint8_t)(dmg + 2);   /* strength bonus */
    else if (at_str >= 14) dmg++;
    hit_monster(mi, dmg);
    if (mt->corr && rn2(2))         /* acid eats the weapon you strike it with */
        corrode_worn(')');
    if (mt->ch == 'e' && !st_blind) {
        /* you met the floating eye's gaze mid-swing -- the classic freeze.
         * A blind hero can't meet it (and telepathy makes blind-fighting
         * eyes the NetHack-approved trick). */
        st_sleep = (uint8_t)(st_sleep + rn2(5) + 2);
        msg("You are frozen by its gaze!");
    }
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
        switch (mt->atk) {          /* special on-hit effects (status-effect layer) */
        case ATK_POISON:
            if (intrinsics & INTR_POISON_RES) break;   /* immune flesh */
            if (rn2(2)) { st_poison = (uint8_t)(st_poison + rn2(4) + 3);
                          msg("You feel poisoned!"); }
            break;
        case ATK_BLIND:
            if (rn2(2)) { st_blind = (uint8_t)(st_blind + rn2(15) + 10);
                          map_dirty = 1; msg("You are blinded!"); }
            break;
        case ATK_STEAL:
            if (gold > 0) {
                gold = (uint16_t)(gold >> 1);   /* grabs about half of it... */
                m_alive[i] = 0;                 /* ...then vanishes with the loot */
                msg2("The ", mt->name, " steals your gold!");
            }
            break;
        case ATK_SLEEP:
            if (intrinsics & INTR_SLEEP_RES) break;    /* wide awake */
            if (rn2(3) == 0) { st_sleep = (uint8_t)(st_sleep + rn2(4) + 3);
                               msg("You are put to sleep!"); }
            break;
        case ATK_DRAIN:
            if (rn2(2) && pmaxhp > 2) {         /* a wraith saps your life force */
                pmaxhp--;
                if (php > pmaxhp) php = pmaxhp;
                msg("You feel drained!");
            }
            break;
        }
    }
}

/* ---- pathfinding: a BFS distance field from the hero ("Dijkstra map").
 * Computed once per turn; each monster then steps to the neighbouring cell
 * with the smallest distance, which routes optimally around walls.        */

#define UNREACH 255
#ifndef __ZXNEXT
/* +zx (3.5 MHz) only: cap the BFS this far from the hero, and skip it unless a
 * live ENEMY is within MON_WAKE. The dog heels greedily without the flood, and
 * wakes it for itself only when a wall boxes the greedy step (see monsters_turn).
 * The Next (28 MHz, whole 80-wide map visible) keeps the unbounded chase. */
#define MAXDIST  30
#define MON_WAKE 22
#endif
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

#ifndef __ZXNEXT
/* Hand-written Z80 fill of the 1680-byte dist[] (puttile_asm.asm). Clearing the
 * whole map to UNREACH every turn is the BFS's biggest cost on the 3.5 MHz 128K
 * -- profiled at 1680 cell-writes/turn vs the flood's ~112 -- and a tight
 * unrolled fill beats SDCC's scalar loop ~3x. The Next (28 MHz) keeps the C
 * loop. Keep the 1680 in dist_clear in sync with MAPH*MAPW. */
extern void dist_clear(uint8_t *p);
#endif

static void compute_dist_map(void)
{
    uint16_t head = 0, tail = 0;
    uint8_t *d = (uint8_t *)dist;      /* flat view, fast indexing */
    const char *lf = (const char *)lvl;

#ifdef __ZXNEXT
    { uint16_t k; for (k = 0; k < (uint16_t)(MAPH * MAPW); k++) d[k] = UNREACH; }
#else
    dist_clear(d);
#endif

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
#ifdef __ZXNEXT
                if (tail < BFSQ_SIZE)
#else
                if (nd < MAXDIST && tail < BFSQ_SIZE)   /* don't expand past MAXDIST */
#endif
                    bfsq[tail++] = (uint16_t)(((uint16_t)ny << 8) | (uint8_t)nx);
            }
        }
    }
}

/* Step one cell down the BFS gradient toward the hero (the lowest-distance free
 * neighbour). Shared by ordinary monsters and by the pet's "follow" mode. */
static void step_to_hero(uint8_t i)
{
    uint8_t bestd = dist[(uint16_t)m_y[i] * MAPW + m_x[i]];   /* our current distance */
    int bestx = -1, besty = -1, dx, dy;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = (int)m_x[i] + dx, ny = (int)m_y[i] + dy;
            uint8_t nd;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            nd = dist[(uint16_t)ny * MAPW + nx];
            if (nd == UNREACH || nd >= bestd) continue;
            {   /* don't stack -- except the pet, which DISPLACES the keeper
                 * (a swapped keeper parked on the door would seal the shop) */
                int mj = monster_at(nx, ny);
                if (mj >= 0 &&
                    !(i == pet_idx && m_type[mj] == MON_KEEPER)) continue;
            }
            if (i != pet_idx && shop_in_room(nx, ny))
                continue;                            /* shops are an enemy safe zone -- but your pet follows you in */
            bestd = nd; bestx = nx; besty = ny;
        }
    }
    if (bestx >= 0) {
        int mj = monster_at(bestx, besty);
        if (mj >= 0) { m_x[mj] = m_x[i]; m_y[mj] = m_y[i]; } /* keeper steps aside */
        m_x[i] = (uint8_t)bestx; m_y[i] = (uint8_t)besty;
    }
}

/* ---- pet AI: bite an adjacent enemy, else heel by the hero ---- */

/* the pet bites enemy ti; the enemy may bite back and the dog can fall */
static void pet_hits(uint8_t pi, uint8_t ti)
{
    const MonType *mt = mon_find(m_type[ti]);
    uint8_t dmg = (uint8_t)(rn2(4) + 2);     /* 2..5; no hero XP for a pet kill */

    if (m_hp[ti] <= dmg) {
        m_alive[ti] = 0;
        if (dlvl <= MAXLVL)
            mon_dead[dlvl] |= (uint8_t)(1u << ti);
        if (rn2(2))                        /* the dog's kill may leave a corpse */
            corpse_drop(m_x[ti], m_y[ti], m_type[ti]);
        msg2("Your dog kills the ", mt->name, "!");
        sfx_kill();
        return;
    }
    m_hp[ti] = (uint8_t)(m_hp[ti] - dmg);
    if (rn2(2)) {                            /* the cornered enemy bites back */
        uint8_t back = (uint8_t)(rn2(mt->dmg) + 1);
        if (pet_hp <= back) {                /* the dog is slain */
            m_alive[pi] = 0; pet_idx = -1; have_pet = 0; pet_hp = 0;
            msg("Your dog dies.");
        } else {
            pet_hp = (uint8_t)(pet_hp - back);
        }
    }
}

/* the dog bites the first adjacent enemy (enemies converge on the hero, so the
 * dog heeling at your side meets them there); 1 if it bit, else 0 */
static uint8_t pet_bite_adjacent(uint8_t i)
{
    uint8_t j;
    for (j = 0; j < mcount; j++) {
        if (!m_alive[j] || j == pet_idx || m_type[j] == MON_KEEPER) continue;
        if (iabs((int)m_x[j] - (int)m_x[i]) <= 1 &&
            iabs((int)m_y[j] - (int)m_y[i]) <= 1) { pet_hits(i, j); return 1; }
    }
    return 0;
}

static void pet_step(uint8_t i)
{
    uint8_t cur;
    if (pet_bite_adjacent(i)) return;
    cur = dist[(uint16_t)m_y[i] * MAPW + m_x[i]];  /* else heel: keep ~2 cells back */
    if (cur == UNREACH || cur > 2) step_to_hero(i);
}

#ifndef __ZXNEXT
/* +zx: heel the dog toward the hero WITHOUT the BFS. When no enemy is near we
 * skip compute_dist_map entirely (its flood over a big room is the 3.5 MHz
 * movement bottleneck), so the pet can't read the distance gradient -- it steps
 * greedily by line of sight instead. Open rooms (where speed actually matters)
 * route fine; in a corridor tangle the dog may briefly lag, then rejoins once an
 * enemy wakes the real chase, or on the next level (place_pet). */
static uint8_t pet_heel_greedy(uint8_t i)   /* 1 = heeled/stepped, 0 = boxed in */
{
    int hx = hero_x, hy = hero_y;
    int dh = iabs(hx - (int)m_x[i]), dv = iabs(hy - (int)m_y[i]);
    int bestc, dx, dy, bestx = -1, besty = -1;
    if (dh <= 2 && dv <= 2) {
        /* nominally at heel -- but Chebyshev ignores WALLS: parked one wall
         * away (dog inside the shop, hero just outside its second door) the
         * dog believed it was beside you forever. Heel only counts when we
         * are adjacent or the one step toward you is open floor; otherwise
         * fall through -- the greedy finds nothing strictly closer, returns
         * 0, and the caller's BFS routes us around through the door. */
        char t;
        if (dh <= 1 && dv <= 1) return 1;
        t = lvl[(int)m_y[i] + ((hy > (int)m_y[i]) - (hy < (int)m_y[i]))]
               [(int)m_x[i] + ((hx > (int)m_x[i]) - (hx < (int)m_x[i]))];
        if (t != '|' && t != '-' && t != ' ') return 1;
    }
    bestc = (dh > dv) ? dh : dv;                /* current Chebyshev distance     */
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = (int)m_x[i] + dx, ny = (int)m_y[i] + dy, a, b, c, mj;
            char t;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            a = iabs(hx - nx); b = iabs(hy - ny);   /* distance filter FIRST (cheap) */
            c = (a > b) ? a : b;
            if (c >= bestc) continue;
            t = lvl[ny][nx];
            if (t == '|' || t == '-' || t == ' ') continue;   /* wall/rock     */
            if (nx == hx && ny == hy) continue;               /* not onto hero */
            mj = monster_at(nx, ny);
            if (mj >= 0 && m_type[mj] != MON_KEEPER) continue; /* don't stack --
                                 * but the pet DISPLACES the keeper (below):
                                 * a swapped keeper parked on the shop door
                                 * would seal the dog in or out forever */
            bestc = c; bestx = nx; besty = ny;
        }
    }
    if (bestx >= 0) {
        int mj = monster_at(bestx, besty);
        if (mj >= 0) { m_x[mj] = m_x[i]; m_y[mj] = m_y[i]; }  /* keeper steps aside */
        m_x[i] = (uint8_t)bestx; m_y[i] = (uint8_t)besty;
        return 1;
    }
    return 0;                          /* boxed in: caller floods once and routes */
}

/* +zx: an enemy chases the hero greedily by line of sight (no BFS). In the open
 * room where it matters for speed this always finds a closer cell, so the flood
 * never runs -- that flood over a big room is why a single visible monster lagged
 * the whole room. Returns 0 only when a wall boxes every closer step, leaving the
 * BFS to route it (corridor floods are small). The caller handles adjacency. */
static uint8_t enemy_chase_greedy(uint8_t i)
{
    int hx = hero_x, hy = hero_y;
    int dh = iabs(hx - (int)m_x[i]), dv = iabs(hy - (int)m_y[i]);
    int curc = (dh > dv) ? dh : dv;                /* must beat this Chebyshev */
    int bestc = 0, bestm = 0, dx, dy, bestx = -1, besty = -1;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = (int)m_x[i] + dx, ny = (int)m_y[i] + dy, a, b, c, m;
            char t;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            /* distance filters FIRST: they reject most neighbours for a couple of
             * adds, so the pricier probes below run for ~2 real candidates, not 8 */
            a = iabs(hx - nx); b = iabs(hy - ny);
            c = (a > b) ? a : b;
            if (c >= curc) continue;                          /* must get closer    */
            m = a + b;            /* Manhattan tiebreak: among equal Chebyshev, the
                                   * straighter step -- else it drifts diagonally   */
            if (bestx >= 0 && (c > bestc || (c == bestc && m >= bestm)))
                continue;                                     /* not better anyway  */
            t = lvl[ny][nx];
            if (t == '|' || t == '-' || t == ' ') continue;   /* wall/rock         */
            if (nx == hx && ny == hy) continue;               /* attack is separate */
            if (shop_in_room(nx, ny)) continue;               /* shops are a safe zone */
            if (monster_at(nx, ny) >= 0) continue;            /* don't stack         */
            bestc = c; bestm = m; bestx = nx; besty = ny;
        }
    }
    if (bestx >= 0) { m_x[i] = (uint8_t)bestx; m_y[i] = (uint8_t)besty; return 1; }
    return 0;
}
#endif

/* a monster steps to the neighbour closest to the hero (lowest distance) */
static void mon_step(uint8_t i)
{
    int ddx, ddy;

    if (m_type[i] == MON_KEEPER) return;   /* the shopkeeper never moves */
    if (m_type[i] == 'e') return;          /* the floating eye just floats */
    if (m_sleep[i]) { m_sleep[i]--; return; }    /* asleep (wand of sleep): no turn */
    if (i == pet_idx) { pet_step(i); return; }   /* the pet follows its own rules */

    ddx = hero_x - (int)m_x[i];
    ddy = hero_y - (int)m_y[i];

    if (iabs(ddx) <= 1 && iabs(ddy) <= 1) {   /* adjacent -> attack */
        if (el_life && hero_x == el_x && hero_y == el_y)
            return;                           /* Elbereth: it dares not strike */
        monster_hits_player(i);
        return;
    }

    step_to_hero(i);
}

void monsters_turn(void) __banked
{
    uint8_t i;
#ifndef __ZXNEXT
    /* +zx: skip the whole chase when no ENEMY is near. The pet (always at your
     * heel) and the stationary shopkeeper must NOT count here -- otherwise the
     * BFS would flood every single turn, which is exactly what made big rooms
     * crawl once the dog arrived (v1.4.0). With no enemy awake we only heel the
     * dog, greedily, and skip compute_dist_map. */
    uint8_t awake = 0;
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i] || i == pet_idx || m_type[i] == MON_KEEPER) continue;
        if (iabs((int)m_x[i] - hero_x) <= MON_WAKE &&
            iabs((int)m_y[i] - hero_y) <= MON_WAKE) { awake = 1; break; }
    }
    if (!awake) {
        /* no enemy near: heel the dog by sight, no flood. Only when a wall boxes
         * the greedy step (a bend or doorway it can't round straight to you) do we
         * flood -- once, routing just the dog. An open room never blocks, so it
         * never pays; a corridor's flood is small. This keeps the dog ~2 cells back
         * everywhere instead of letting the gap grow turn by turn through bends. */
        if (pet_idx < 0 || !m_alive[(uint8_t)pet_idx]) return;
        if (pet_heel_greedy((uint8_t)pet_idx)) return;
        compute_dist_map();
        pet_step((uint8_t)pet_idx);
        return;
    }
    /* an enemy IS near: every monster chases GREEDILY by line of sight (no flood).
     * The BFS runs only for any that a wall boxes in -- an open room never blocks,
     * so a single visible monster no longer floods the whole big room each turn,
     * which is what made it lag. */
    {
        uint16_t blocked = 0;
        for (i = 0; i < mcount; i++) {
            if (!m_alive[i] || m_type[i] == MON_KEEPER) continue;
            if (m_type[i] == 'e') continue;   /* the floating eye just floats */
            if (m_sleep[i]) { m_sleep[i]--; continue; }
            if (i == (uint8_t)pet_idx) {
                if (pet_bite_adjacent(i)) continue;
                if (!pet_heel_greedy(i)) blocked |= (uint16_t)(1u << i);
                continue;
            }
            if (iabs((int)m_x[i] - hero_x) > MON_WAKE ||
                iabs((int)m_y[i] - hero_y) > MON_WAKE) continue;   /* still dormant */
            if (iabs(hero_x - (int)m_x[i]) <= 1 && iabs(hero_y - (int)m_y[i]) <= 1) {
                if (el_life && hero_x == el_x && hero_y == el_y) continue;  /* Elbereth */
                monster_hits_player(i);
                if (dead) return;
                continue;
            }
            if (!enemy_chase_greedy(i)) blocked |= (uint16_t)(1u << i);
        }
        if (blocked) {                          /* route only the wall-boxed ones */
            compute_dist_map();
            for (i = 0; i < mcount; i++) {
                if (!(blocked & (uint16_t)(1u << i)) || !m_alive[i]) continue;
                if (i == (uint8_t)pet_idx) pet_step(i);
                else                       step_to_hero(i);
            }
        }
    }
    return;
#endif
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
    m_sleep[slot] = 0;          /* a fresh wanderer is awake */
    if (slot == mcount) mcount++;
}
