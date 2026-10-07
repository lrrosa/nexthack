/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* levelfov.c - BANKED field-of-view + save/restore, split out of level.c.
 *
 * None of this is per-cell hot: fov_update runs once per turn; draw_map calls
 * fov_bitmap()/vis_bitmap() once per redraw and then reads the returned bitmap
 * inline. So all of it can be banked (see banks.json) -- the entry points are
 * __banked. The fog-of-war pool is DATA and stays resident, so the inline
 * per-cell reads in (banked) draw_map cost nothing. The room table r_*[] and
 * the persistence masks come from levelgen.c via extern. */

#include "level.h"
#include "platform.h"     /* file_read/file_write */
#include "game.h"         /* dlvl, MAXLVL          */
#include <string.h>       /* memset                */

extern uint8_t r_x[], r_y[], r_w[], r_h[];   /* room rects (levelgen.c) */
extern uint8_t gold_taken[], item_taken[];   /* persistence masks (levelgen.c) */

/* ---- field of view --------------------------------------------------------
 * Explored cells ("seen") are remembered per depth in a compact bitmap, so a
 * revisited level shows up the way you left it. Current visibility is derived
 * each turn from the hero's room (whole room lights up) plus a radius of 1. */

#define FOV_BYTES ((MAPW * MAPH + 7) / 8)        /* 210 bytes per level */

/* Keeping the fog-of-war for every level (210 bytes each) costs too much RAM in
 * a deep dungeon, so the explored bitmaps live in an LRU pool: only the most
 * recently visited FOV_SLOTS levels stay resident. A level evicted from the pool
 * forgets its map (it shows unexplored again if revisited later). The cheap
 * per-level bitmasks (gold/item/monster kills) are still kept for every level. */
#define FOV_SLOTS 12  /* remember the 12 most recently visited levels' maps
                       * (was 4 in the resident-pressure era; the v0.9 item.c
                       * reclaim bought the room back). 12 slots = 2520 B; the
                       * pool is in the save, so growing this bumps SAVE_VER. */

/* Where the LRU explored bitmaps live differs per target:
 *  - 128K: data-banked in Bank 5 (always mapped at 0x4000-0x7FFF) at 0x68A0,
 *    just past the mirrored-UDG annex (0x6888..0x68A0); 12 slots end at 0x7278,
 *    below the renderer's PREV_VIS copy (0x7280) and the BFS scratch at
 *    0x7400 -- zero resident cost. FLAT view
 *    (SDCC rejects pointer-to-array casts): index as
 *    fov_pool[(uint16_t)slot*FOV_BYTES + byte].
 *  - Next: Bank 5's tail is full (tiles+inv below the 0x5C00 sysvars, tilemap
 *    at 0x6000, BFS at 0x7400), so the grown pool lives in resident BSS --
 *    the item.c reclaim is what pays for it. */
#ifndef __ZXNEXT
#define fov_pool ((uint8_t *)0x68A0u)
#else
static uint8_t fov_pool_buf[(uint16_t)FOV_SLOTS * FOV_BYTES];
#define fov_pool fov_pool_buf
#endif
static uint8_t  slot_lvl[FOV_SLOTS];             /* dlvl in each slot (0 = free) */
static uint16_t slot_tick[FOV_SLOTS];            /* last-touched time, for LRU   */
static uint16_t fov_clock;                       /* advances on each level entry */
static uint8_t  cur_slot;                        /* pool slot for the current dlvl */
static uint8_t  last_dlvl;                        /* dlvl cur_slot was resolved for */

/* ---- dug cells (1.6) -------------------------------------------------------
 * The pick-axe digs sideways through rock and walls, and a level is rebuilt
 * from its seed on every visit -- so a tunnel must be remembered, as a forced
 * door or a taken pile is. Not by bit this time: there is no fixed list of
 * cells a hero might dig. A record is (level, y*MAPW + x) in 3 bytes, and
 * build_level re-opens this level's cells once the level is otherwise final
 * (dug_restore), so generation, spawning and every persistence index see the
 * level exactly as it was born. Every dug cell becomes corridor: a breach in
 * a wall is never '.', where the trap hash (nexthack.c trap_type) would hide
 * a trap in one cell in 47 of them.
 *
 * The pool is in Bank 5's free tail on both targets (zero resident cost),
 * above the BFS queue: dug_pool[0] is the count, the records follow, oldest
 * first. Full, it forgets the oldest record of another level -- the one place
 * a tunnel can close, and only on a floor the hero is not standing on. It is
 * in the save (level_save), so growing DUG_MAX bumps SAVE_VER. */
#define DUG_MAX  128
#define dug_pool ((uint8_t *)0x7C90u)    /* 1 + DUG_MAX*3 B: ends 0x7E11 */

/* the hero has dug (x,y) open on this level: remember it */
void dug_add(uint8_t x, uint8_t y) __banked
{
    uint8_t  i, n = dug_pool[0];
    uint8_t *p = dug_pool + 1;
    uint16_t c = (uint16_t)y * MAPW + x;

    if (n == DUG_MAX) {
        for (i = 0; i < n && p[0] == (uint8_t)dlvl; i++) p += 3;
        if (i == n) return;             /* every record is this floor's: the
                                         * new hole goes unremembered instead */
        for (n--; i < n; i++, p += 3) { /* drop record i; the rest move down */
            p[0] = p[3]; p[1] = p[4]; p[2] = p[5];
        }
    }
    p = dug_pool + 1 + (uint16_t)n * 3;
    p[0] = (uint8_t)dlvl;
    p[1] = (uint8_t)c;
    p[2] = (uint8_t)(c >> 8);
    dug_pool[0] = (uint8_t)(n + 1);
}

/* re-open this level's tunnels (build_level, before the floor stash) */
void dug_restore(void) __banked
{
    uint8_t  i, n = dug_pool[0];
    const uint8_t *p = dug_pool + 1;
    for (i = 0; i < n; i++, p += 3)
        if (p[0] == (uint8_t)dlvl)
            ((char *)lvl)[p[1] | ((uint16_t)p[2] << 8)] = '#';
}

static int     hero_room = -1;
static int     fov_hx, fov_hy;

static uint8_t vis_now[FOV_BYTES];   /* cells visible right now (this turn) */

/* A rolling hash of vis_now, so draw_map can tell when the visible set is
 * UNCHANGED from last turn (you moved within a lit room): then it repaints only
 * the hero and moved monsters instead of all 672 viewport cells. */
uint16_t fov_vis_sum;
static void fov_recalc_sum(void)
{
    const uint8_t *p = vis_now;
    uint16_t s = 0;
    uint8_t  n = FOV_BYTES;
    do { s = (uint16_t)((s << 8) + s + *p++); } while (--n);
    fov_vis_sum = s;
}

/* Make the current dlvl own cur_slot, evicting the least-recently-used level
 * when the pool is full. The last_dlvl fast-path keeps this ~free per turn. */
static void fov_touch(void)
{
    uint8_t i, victim;
    uint8_t d = (uint8_t)dlvl;

    if (last_dlvl == d) return;                  /* same floor as last turn */
    last_dlvl = d;

    for (i = 0; i < FOV_SLOTS; i++)              /* already resident? */
        if (slot_lvl[i] == d) {
            cur_slot = i;
            slot_tick[i] = ++fov_clock;
            return;
        }

    victim = 0;                                  /* a free slot, else evict LRU */
    for (i = 0; i < FOV_SLOTS; i++) {
        if (slot_lvl[i] == 0) { victim = i; break; }
        if (slot_tick[i] < slot_tick[victim]) victim = i;
    }
    { uint16_t base = (uint16_t)victim * FOV_BYTES, b;
      for (b = 0; b < FOV_BYTES; b++) fov_pool[base + b] = 0; }
    slot_lvl[victim]  = d;
    slot_tick[victim] = ++fov_clock;
    cur_slot = victim;
}

static uint8_t *fov_map(void)        /* explored bitmap for the current depth */
{
    return fov_pool + (uint16_t)cur_slot * FOV_BYTES;
}

/* The current depth's explored bitmap, resolved once per fov_update: light()
 * used to call fov_map() -- a slot * 210 multiply -- for every cell it lit. */
static uint8_t *fov_fm;

/* mark a cell as visible this turn and remembered (seen) */
static void light(int x, int y)
{
    uint16_t idx;
    uint8_t  bit;
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) return;
    idx = (uint16_t)y * MAPW + x;
    bit = (uint8_t)(1u << (idx & 7));
    vis_now[idx >> 3] |= bit;
    fov_fm[idx >> 3] |= bit;
}

/* set a run of n consecutive bits from bit index `start` in bitmap bm. Lighting
 * a room row this way -- whole bytes at a time -- instead of one light() per cell
 * is ~10x fewer writes, which is what made a big lit room crawl on the 3.5 MHz
 * 128K. MAPW (80) is a multiple of 8, so every row starts byte-aligned and a run
 * never spills into another row. */
static void set_run(uint8_t *bm, uint16_t start, uint16_t n)
{
    uint16_t end = start + n;
    while ((start & 7) && start < end) { bm[start >> 3] |= (uint8_t)(1u << (start & 7)); start++; }
    while (start + 8 <= end)           { bm[start >> 3]  = 0xFF;                          start += 8; }
    while (start < end)                { bm[start >> 3] |= (uint8_t)(1u << (start & 7)); start++; }
}

static int in_room(uint8_t r, int x, int y)
{
    return x >= r_x[r] && x < r_x[r] + r_w[r] &&
           y >= r_y[r] && y < r_y[r] + r_h[r];
}

void fov_reset(void) __banked   /* forget every level's exploration (new game) */
{                               /* -- and its tunnels, which Bank 5 keeps */
    uint8_t i;
    for (i = 0; i < FOV_SLOTS; i++) {
        slot_lvl[i]  = 0;
        slot_tick[i] = 0;
    }
    dug_pool[0] = 0;
    fov_clock = 0;
    cur_slot  = 0;
    last_dlvl = 0;
    hero_room = -1;
}

/* eight ray directions for corridor line-of-sight */
static const signed char RDX[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
static const signed char RDY[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };
#define SIGHT 12

void fov_update(int hx, int hy) __banked
{
    int dx, dy;
    uint8_t r, i;

    fov_touch();                 /* make cur_slot track the current depth */
    fov_fm = fov_map();
    fov_hx = hx; fov_hy = hy;

    memset(vis_now, 0, FOV_BYTES);      /* clear current visibility */

    hero_room = -1;
    for (r = 0; r < rcount; r++)
        if (in_room(r, hx, hy)) { hero_room = r; break; }

    /* blind: you sense only the cell you stand on -- no rooms, no rays, and no
     * new exploration. draw_map then shows the rest from memory (dimmed) and
     * monsters vanish (they are only drawn where currently visible). */
    if (st_blind) { light(hx, hy); fov_recalc_sum(); return; }

    /* radius 1 (the cells immediately around the hero) */
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            light(hx + dx, hy + dy);

    /* if standing in a room, the whole room is lit -- by a bit-run per row (whole
     * bytes at a time) rather than a light() per cell. Same bits set, ~10x fewer
     * writes; on the 3.5 MHz 128K this was the per-move cost in a big lit room. */
    if (hero_room >= 0) {
        uint8_t rr = (uint8_t)hero_room, yy;
        uint8_t x0 = r_x[rr];
        uint8_t xw = r_w[rr];
        uint8_t ymax = (uint8_t)(r_y[rr] + r_h[rr]);
        uint8_t *fm = fov_fm;
        if ((uint16_t)x0 + xw > MAPW) xw = (uint8_t)(MAPW - x0);   /* clamp to map */
        if (ymax > MAPH) ymax = MAPH;
        for (yy = r_y[rr]; yy < ymax; yy++) {
            uint16_t start = (uint16_t)yy * MAPW + x0;
            set_run(vis_now, start, xw);
            set_run(fm, start, xw);
        }
    }

    /* line of sight down corridors: cast rays until a wall blocks them, so
     * monsters chasing single-file are all visible. Each ray walks by adding
     * its step to the cell index -- it used to compute hx + RDX[i] * d, two
     * library multiplies a cell, and redo y * MAPW + x inside light(): on
     * the 3.5 MHz 128K the rays were most of the FOV's cost. The hero is on
     * the map, so a uint8_t coordinate stepping off the left or top edge
     * wraps to 255 and fails the same bound test as the right and bottom. */
    for (i = 0; i < 8; i++) {
        int8_t   sx = RDX[i], sy = RDY[i];
        int16_t  step = sx;
        uint8_t  x = (uint8_t)hx, y = (uint8_t)hy, d;
        uint16_t idx = (uint16_t)hy * MAPW + (uint16_t)hx;
        if (sy > 0) step += MAPW;
        else if (sy < 0) step -= MAPW;
        for (d = 0; d < SIGHT; d++) {
            char c;
            uint8_t bit;
            x = (uint8_t)(x + sx); y = (uint8_t)(y + sy);
            if (x >= MAPW || y >= MAPH) break;
            idx = (uint16_t)(idx + step);
            bit = (uint8_t)(1u << (idx & 7));
            vis_now[idx >> 3] |= bit;
            fov_fm[idx >> 3]  |= bit;
            c = ((const char *)lvl)[idx];
            /* walls, rock and doors are opaque: rays light corridors but do
             * not peek into rooms (a room is revealed when you enter it) */
            if (c == '|' || c == '-' || c == ' ' || c == '+') break;
        }
    }
    fov_recalc_sum();
}

int fov_seen(int x, int y) __banked
{
    uint16_t idx;
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH)
        return 0;
    idx = (uint16_t)y * MAPW + x;
    return (fov_map()[idx >> 3] >> (idx & 7)) & 1;
}

const uint8_t *fov_bitmap(void) __banked
{
    return fov_map();
}

const uint8_t *vis_bitmap(void) __banked
{
    return vis_now;
}

void fov_reveal(void) __banked   /* magic mapping: remember the whole level */
{
    uint16_t i;
    uint8_t *m = fov_map();
    for (i = 0; i < FOV_BYTES; i++)
        m[i] = 0xFF;
}

/* The attract demo's map: everything but solid rock. It draws exactly like
 * fov_reveal -- rock is a black tile seen or not -- but a remembered rock
 * cell sends the Next's draw_map through tile_for's whole switch, and the
 * demo redraws every step: marking the rock too made each step cost about
 * twice as much. MAPW*MAPH is exactly FOV_BYTES*8 cells, so this is a
 * byte-at-a-time walk of lvl[][]. */
void fov_reveal_built(void) __banked
{
    uint8_t *m = fov_map();
    const char *c = (const char *)lvl;
    uint8_t b, bit, v;
    for (b = 0; b < FOV_BYTES; b++) {
        v = 0;
        for (bit = 1; bit; bit <<= 1)       /* 1, 2 .. 128, then 0 ends it */
            if (*c++ != ' ') v |= bit;
        m[b] |= v;
    }
}

/* Remember every cell of the current level that holds c, wherever it is: the
 * scroll of gold detection ('$') and its confused reading, the traps ('^').
 * The cells then draw as the rest of the remembered map does, dimmed. Returns
 * how many there are (saturating). */
uint8_t fov_reveal_char(char c) __banked
{
    uint8_t *m = fov_map();
    const char *p = (const char *)lvl;
    uint16_t i;
    uint8_t n = 0;
    for (i = 0; i < (uint16_t)(MAPW * MAPH); i++)
        if (p[i] == c) {
            m[i >> 3] |= (uint8_t)(1u << (i & 7));
            if (n < 255) n++;
        }
    return n;
}

void fov_forget(void) __banked   /* amnesia: this level's map is gone */
{
    uint16_t i;
    uint8_t *m = fov_map();
    for (i = 0; i < FOV_BYTES; i++)
        m[i] = 0;
}

int fov_visible(int x, int y) __banked
{
    uint16_t idx;
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH)
        return 0;
    idx = (uint16_t)y * MAPW + x;
    return (vis_now[idx >> 3] >> (idx & 7)) & 1;
}

/* ---- save / restore: per-depth gold/item bitmasks + explored bitmaps ---- */
void level_save(uint8_t h) __banked
{
    file_write(h, gold_taken, MAXLVL + 1);
    file_write(h, item_taken, MAXLVL + 1);
    file_write(h, fov_pool,   (uint16_t)(FOV_SLOTS * FOV_BYTES));
    file_write(h, slot_lvl,   FOV_SLOTS);
    file_write(h, slot_tick,  (uint16_t)(FOV_SLOTS * 2));
    file_write(h, &fov_clock, 2);
    file_write(h, dug_pool,   1 + DUG_MAX * 3);
}

void level_load(uint8_t h) __banked
{
    file_read(h, gold_taken, MAXLVL + 1);
    file_read(h, item_taken, MAXLVL + 1);
    file_read(h, fov_pool,   (uint16_t)(FOV_SLOTS * FOV_BYTES));
    file_read(h, slot_lvl,   FOV_SLOTS);
    file_read(h, slot_tick,  (uint16_t)(FOV_SLOTS * 2));
    file_read(h, &fov_clock, 2);
    file_read(h, dug_pool,   1 + DUG_MAX * 3);
    last_dlvl = 0;              /* force fov_touch to re-resolve cur_slot */
}
