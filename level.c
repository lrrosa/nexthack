/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* level.c - HOT half of the level module: the terrain buffer, the terrain
 * accessors used every turn (terrain/walkable/tile_for), the field of view
 * (recomputed each turn) and save/restore. This half stays RESIDENT.
 *
 * The COLD half - procedural generation and gold/item persistence - lives in
 * levelgen.c (a banked module). It owns the room table and persistence masks;
 * we read them here via extern (DATA is resident even for banked code). */

#include "level.h"
#include "platform.h"     /* T_* tile numbers, file_read/file_write */
#include "game.h"         /* dlvl, MAXLVL                           */

char    lvl[MAPH][MAPW];
uint8_t rcount;
uint8_t up_x, up_y, dn_x, dn_y;

/* Owned by levelgen.c (the cold half), read here: the room rectangles by FOV,
 * the per-depth persistence masks by save/load. */
extern uint8_t r_x[], r_y[], r_w[], r_h[];
extern uint8_t gold_taken[], item_taken[];

char terrain(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH)
        return ' ';
    return lvl[y][x];
}

int walkable(char c)
{
    return !(c == '|' || c == '-' || c == ' ');
}

uint8_t tile_for(char c)
{
    switch (c) {
    case '.': return T_FLOOR;
    case '#': return T_CORR;
    case '-':
    case '|': return T_WALL;
    case '+': return T_DOOR;
    case '<': return T_SUP;
    case '>': return T_SDOWN;
    case '$': return T_GOLD;
    case '%': return T_FOOD;
    case ')': return T_WEAPON;
    case '[': return T_ARMOR;
    case '!': return T_POTION;
    case '?': return T_SCROLL;
    case '=': return T_RING;
    case '"': return T_AMULET;
    default:  return T_ROCK;
    }
}

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
#define FOV_SLOTS 8   /* remember the 8 most recently visited levels' maps;
                       * kept well below the RAM max to reserve BSS for future
                       * features (see the memory budget note). */

static uint8_t  fov_pool[FOV_SLOTS][FOV_BYTES];  /* resident explored bitmaps   */
static uint8_t  slot_lvl[FOV_SLOTS];             /* dlvl in each slot (0 = free) */
static uint16_t slot_tick[FOV_SLOTS];            /* last-touched time, for LRU   */
static uint16_t fov_clock;                       /* advances on each level entry */
static uint8_t  cur_slot;                        /* pool slot for the current dlvl */
static uint8_t  last_dlvl;                        /* dlvl cur_slot was resolved for */

static int     hero_room = -1;
static int     fov_hx, fov_hy;

static uint8_t vis_now[FOV_BYTES];   /* cells visible right now (this turn) */

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
    { uint16_t b; for (b = 0; b < FOV_BYTES; b++) fov_pool[victim][b] = 0; }
    slot_lvl[victim]  = d;
    slot_tick[victim] = ++fov_clock;
    cur_slot = victim;
}

static uint8_t *fov_map(void)        /* explored bitmap for the current depth */
{
    return fov_pool[cur_slot];
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

static int in_room(uint8_t r, int x, int y)
{
    return x >= r_x[r] && x < r_x[r] + r_w[r] &&
           y >= r_y[r] && y < r_y[r] + r_h[r];
}

void fov_reset(void)        /* forget every level's exploration (new game) */
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

void fov_update(int hx, int hy)
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

    /* radius 1 (the cells immediately around the hero) */
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            light(hx + dx, hy + dy);

    /* if standing in a room, the whole room is lit */
    if (hero_room >= 0) {
        uint8_t rr = (uint8_t)hero_room, xx, yy;
        for (yy = r_y[rr]; yy < r_y[rr] + r_h[rr]; yy++)
            for (xx = r_x[rr]; xx < r_x[rr] + r_w[rr]; xx++)
                light(xx, yy);
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
}

int fov_seen(int x, int y)
{
    uint16_t idx;
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH)
        return 0;
    idx = (uint16_t)y * MAPW + x;
    return (fov_map()[idx >> 3] >> (idx & 7)) & 1;
}

const uint8_t *fov_bitmap(void)
{
    return fov_map();
}

const uint8_t *vis_bitmap(void)
{
    return vis_now;
}

void fov_reveal(void)        /* magic mapping: remember the whole level */
{
    uint16_t i;
    uint8_t *m = fov_map();
    for (i = 0; i < FOV_BYTES; i++)
        m[i] = 0xFF;
}

int fov_visible(int x, int y)
{
    uint16_t idx;
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH)
        return 0;
    idx = (uint16_t)y * MAPW + x;
    return (vis_now[idx >> 3] >> (idx & 7)) & 1;
}

/* ---- save / restore: per-depth gold/item bitmasks + explored bitmaps ----
 * These are cold but stay resident (they touch the FOV pool, which is local to
 * this file); keeping them here avoids exporting the FOV internals. */
void level_save(uint8_t h)
{
    file_write(h, gold_taken, MAXLVL + 1);
    file_write(h, item_taken, MAXLVL + 1);
    file_write(h, fov_pool,   (uint16_t)(FOV_SLOTS * FOV_BYTES));
    file_write(h, slot_lvl,   FOV_SLOTS);
    file_write(h, slot_tick,  (uint16_t)(FOV_SLOTS * 2));
    file_write(h, &fov_clock, 2);
}

void level_load(uint8_t h)
{
    file_read(h, gold_taken, MAXLVL + 1);
    file_read(h, item_taken, MAXLVL + 1);
    file_read(h, fov_pool,   (uint16_t)(FOV_SLOTS * FOV_BYTES));
    file_read(h, slot_lvl,   FOV_SLOTS);
    file_read(h, slot_tick,  (uint16_t)(FOV_SLOTS * 2));
    file_read(h, &fov_clock, 2);
    last_dlvl = 0;              /* force fov_touch to re-resolve cur_slot */
}
