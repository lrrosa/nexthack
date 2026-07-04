/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* levelfov.c - BANKED field-of-view + save/restore, split out of level.c.
 *
 * None of this is per-cell hot: fov_update runs once per turn; draw_map calls
 * fov_bitmap()/vis_bitmap() once per redraw and then reads the returned bitmap
 * inline. So all of it can be banked (PAGE_20_CODE) -- the entry points are
 * __banked. The fog-of-war pool is DATA and stays resident, so the inline
 * per-cell reads in (banked) draw_map cost nothing. The room table r_*[] and
 * the persistence masks come from levelgen.c via extern. */

#include "level.h"
#include "platform.h"     /* file_read/file_write */
#include "game.h"         /* dlvl, MAXLVL          */

#ifdef __ZXNEXT
#pragma codeseg PAGE_26_CODE   /* moved off PAGE_20 (bank 10 filled up as item.c
                                * grew); shares bank 13 with leveltmpl/monster_ai */
#else
#pragma codeseg BANK_4   /* moved out of BANK_1 when item.c's growth filled it
                          * (BANK_4 holds the SCR screens with ~2.5 KB spare) */
#endif

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
#define FOV_SLOTS 4   /* remember the 4 most recently visited levels' maps.
                       * (Was 8, then 6, then 5.) The pool itself is now data-
                       * banked into Bank 5 (below), so this no longer costs
                       * resident RAM; 4 slots = 840 B, which is what fits after
                       * inv on the Next before the sysvars. */

/* The LRU explored bitmaps live in Bank 5 (always mapped at 0x4000-0x7FFF on
 * both targets), like inv -- so the ~840 B cost no resident BSS. They sit just
 * past inv: Next 0x5880 (inv 0x5800..0x5878; NextZXOS sysvars at 0x5C00, 56 B
 * clear) / 128K 0x6880 (inv 0x6800..; BFS scratch far above at 0x7400). FLAT
 * view -- SDCC rejects pointer-to-array casts -- so index as
 * fov_pool[(uint16_t)slot*FOV_BYTES + byte]. Save format is unchanged (the same
 * FOV_SLOTS*FOV_BYTES bytes are written), so no SAVE_VER bump. */
#ifndef __ZXNEXT
#define fov_pool ((uint8_t *)0x6880u)
#else
#define fov_pool ((uint8_t *)0x5880u)
#endif
static uint8_t  slot_lvl[FOV_SLOTS];             /* dlvl in each slot (0 = free) */
static uint16_t slot_tick[FOV_SLOTS];            /* last-touched time, for LRU   */
static uint16_t fov_clock;                       /* advances on each level entry */
static uint8_t  cur_slot;                        /* pool slot for the current dlvl */
static uint8_t  last_dlvl;                        /* dlvl cur_slot was resolved for */

static int     hero_room = -1;
static int     fov_hx, fov_hy;

static uint8_t vis_now[FOV_BYTES];   /* cells visible right now (this turn) */

/* A rolling hash of vis_now, so draw_map can tell when the visible set is
 * UNCHANGED from last turn (you moved within a lit room): then it repaints only
 * the hero and moved monsters instead of all 672 viewport cells. */
uint16_t fov_vis_sum;
static void fov_recalc_sum(void)
{
    uint16_t s = 0; uint8_t b;
    for (b = 0; b < FOV_BYTES; b++) s = (uint16_t)((s << 8) + s + vis_now[b]);
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

/* mark a cell as visible this turn and remembered (seen) */
static void light(int x, int y)
{
    uint16_t idx;
    uint8_t  bit;
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) return;
    idx = (uint16_t)y * MAPW + x;
    bit = (uint8_t)(1u << (idx & 7));
    vis_now[idx >> 3] |= bit;
    fov_map()[idx >> 3] |= bit;
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
{
    uint8_t i;
    for (i = 0; i < FOV_SLOTS; i++) {
        slot_lvl[i]  = 0;
        slot_tick[i] = 0;
    }
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
    int dx, dy, d;
    uint8_t r, i, b;

    fov_touch();                 /* make cur_slot track the current depth */
    fov_hx = hx; fov_hy = hy;

    for (b = 0; b < FOV_BYTES; b++)     /* clear current visibility */
        vis_now[b] = 0;

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
        uint8_t *fm = fov_map();
        if ((uint16_t)x0 + xw > MAPW) xw = (uint8_t)(MAPW - x0);   /* clamp to map */
        if (ymax > MAPH) ymax = MAPH;
        for (yy = r_y[rr]; yy < ymax; yy++) {
            uint16_t start = (uint16_t)yy * MAPW + x0;
            set_run(vis_now, start, xw);
            set_run(fm, start, xw);
        }
    }

    /* line of sight down corridors: cast rays until a wall blocks them, so
     * monsters chasing single-file are all visible */
    for (i = 0; i < 8; i++) {
        for (d = 1; d <= SIGHT; d++) {
            int nx = hx + RDX[i] * d, ny = hy + RDY[i] * d;
            char c;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) break;
            light(nx, ny);
            c = lvl[ny][nx];
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
}

void level_load(uint8_t h) __banked
{
    file_read(h, gold_taken, MAXLVL + 1);
    file_read(h, item_taken, MAXLVL + 1);
    file_read(h, fov_pool,   (uint16_t)(FOV_SLOTS * FOV_BYTES));
    file_read(h, slot_lvl,   FOV_SLOTS);
    file_read(h, slot_tick,  (uint16_t)(FOV_SLOTS * 2));
    file_read(h, &fov_clock, 2);
    last_dlvl = 0;              /* force fov_touch to re-resolve cur_slot */
}
