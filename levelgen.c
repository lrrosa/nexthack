/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* levelgen.c - COLD half of the level module: procedural dungeon generation
 * and per-level gold/item persistence. Split out of level.c so it can be a
 * whole BANKED module (PAGE_20_CODE), mapped into the 0xC000 window on demand.
 *
 * Its code is cold (runs on level entry / pickup, never per frame), so the
 * trampoline cost is irrelevant. Its DATA stays resident (only code is banked),
 * so the room table r_*[] and the persistence masks gold_taken/item_taken are
 * shared with the HOT half (level.c: FOV reads the rooms, save/load the masks)
 * via plain extern. Public entry points are __banked (see level.h); the static
 * helpers are reached only by in-page calls. */

#include "level.h"
#include "rng.h"          /* rng_set, rn2, world_seed  */
#include "game.h"         /* dlvl, MAXLVL, DLVL_AMULET, has_amulet */

#pragma codeseg PAGE_20_CODE

/* playable area (leaves a rock margin around the edges) */
#define PX0 1
#define PY0 1
#define PX1 78
#define PY1 19

#define SECT_COLS 4
#define SECT_ROWS 2
#define MAXROOMS  (SECT_COLS * SECT_ROWS)

/* Shared with the hot half (level.c). DATA is always resident, so these stay
 * reachable from FOV (rooms) and save/load (masks) even though our code is
 * banked. Defined here; level.c declares them extern. */
uint8_t r_x[MAXROOMS], r_y[MAXROOMS], r_w[MAXROOMS], r_h[MAXROOMS];
uint8_t gold_taken[MAXLVL + 1];    /* bit i: gold pile i collected */
uint8_t item_taken[MAXLVL + 1];    /* bit i: item i picked up      */

/* gold/item positions are generation-private (only cold code touches them) */
static uint8_t gcount;
static uint8_t g_x[8], g_y[8];
static uint8_t icount;
static uint8_t i_x[8], i_y[8];

static uint16_t level_seed(uint16_t d)
{
    return (uint16_t)(world_seed + (uint16_t)(d * 0x9E37u));
}

static void lvl_clear(void)
{
    uint8_t x, y;
    for (y = 0; y < MAPH; y++)
        for (x = 0; x < MAPW; x++)
            lvl[y][x] = ' ';
}

/* carve one cell of a corridor: door through a wall, '#' through rock */
static void carve(uint8_t x, uint8_t y)
{
    char c = lvl[y][x];
    if (c == '-' || c == '|') lvl[y][x] = '+';
    else if (c == ' ')        lvl[y][x] = '#';
}

static void dig_h(uint8_t x0, uint8_t x1, uint8_t y)
{
    if (x0 > x1) { uint8_t t = x0; x0 = x1; x1 = t; }
    for (; x0 <= x1; x0++) carve(x0, y);
}

static void dig_v(uint8_t y0, uint8_t y1, uint8_t x)
{
    if (y0 > y1) { uint8_t t = y0; y0 = y1; y1 = t; }
    for (; y0 <= y1; y0++) carve(x, y0);
}

static void dig_corridor(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    if (rn2(2)) { dig_h(x0, x1, y0); dig_v(y0, y1, x1); }
    else        { dig_v(y0, y1, x0); dig_h(x0, x1, y1); }
}

/* A corridor running along a wall turns the whole wall into a row of doors,
 * which looks odd. Thin those runs: a door flanked by doors/corridor on both
 * sides (same axis) becomes plain corridor, leaving real doors at the ends. */
static int doorish(char c) { return c == '+' || c == '#'; }

static void thin_doors(void)
{
    uint8_t x, y;
    for (y = 1; y < MAPH - 1; y++)
        for (x = 1; x < MAPW - 1; x++)
            if (lvl[y][x] == '+' &&
                ((doorish(lvl[y][x - 1]) && doorish(lvl[y][x + 1])) ||
                 (doorish(lvl[y - 1][x]) && doorish(lvl[y + 1][x]))))
                lvl[y][x] = '#';
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* Pick a door cell on room r's wall facing (tx,ty), plus the rock cell just
 * outside it, so corridors leave through a single door and run in the rock. */
static void room_door(uint8_t r, int tx, int ty,
                      uint8_t *doorx, uint8_t *doory, int *outx, int *outy)
{
    int cx = r_x[r] + r_w[r] / 2;
    int cy = r_y[r] + r_h[r] / 2;

    if (iabs(tx - cx) >= iabs(ty - cy)) {           /* left/right wall */
        uint8_t row = (uint8_t)(r_y[r] + 1 + rn2((uint8_t)(r_h[r] - 2)));
        if (tx >= cx) { *doorx = (uint8_t)(r_x[r] + r_w[r] - 1); *outx = (int)*doorx + 1; }
        else          { *doorx = r_x[r];                        *outx = (int)*doorx - 1; }
        *doory = row; *outy = row;
    } else {                                        /* top/bottom wall */
        uint8_t col = (uint8_t)(r_x[r] + 1 + rn2((uint8_t)(r_w[r] - 2)));
        if (ty >= cy) { *doory = (uint8_t)(r_y[r] + r_h[r] - 1); *outy = (int)*doory + 1; }
        else          { *doory = r_y[r];                        *outy = (int)*doory - 1; }
        *doorx = col; *outx = col;
    }
}

/* join two rooms: a door on each one's facing wall, corridor through the rock */
static void connect_rooms(uint8_t a, uint8_t b)
{
    uint8_t adx, ady, bdx, bdy;
    int aox, aoy, box, boy;
    int acx = r_x[a] + r_w[a] / 2, acy = r_y[a] + r_h[a] / 2;
    int bcx = r_x[b] + r_w[b] / 2, bcy = r_y[b] + r_h[b] / 2;

    room_door(a, bcx, bcy, &adx, &ady, &aox, &aoy);
    room_door(b, acx, acy, &bdx, &bdy, &box, &boy);
    lvl[ady][adx] = '+';
    lvl[bdy][bdx] = '+';
    dig_corridor((uint8_t)aox, (uint8_t)aoy, (uint8_t)box, (uint8_t)boy);
}

static void make_room(uint8_t i, uint8_t sx0, uint8_t sy0, uint8_t sx1, uint8_t sy1)
{
    uint8_t availw = (uint8_t)(sx1 - sx0 + 1);
    uint8_t availh = (uint8_t)(sy1 - sy0 + 1);
    uint8_t w = (uint8_t)(4 + rn2(8));   /* 4..11 */
    uint8_t h = (uint8_t)(3 + rn2(4));   /* 3..6  */
    uint8_t rx, ry, xx, yy;

    if (w > availw - 1) w = (uint8_t)(availw - 1);
    if (h > availh - 1) h = (uint8_t)(availh - 1);
    rx = (uint8_t)(sx0 + rn2((uint8_t)(availw - w)));
    ry = (uint8_t)(sy0 + rn2((uint8_t)(availh - h)));

    for (yy = ry; yy < ry + h; yy++) {
        for (xx = rx; xx < rx + w; xx++) {
            if (yy == ry || yy == ry + h - 1)      lvl[yy][xx] = '-';
            else if (xx == rx || xx == rx + w - 1) lvl[yy][xx] = '|';
            else                                   lvl[yy][xx] = '.';
        }
    }
    r_x[i] = rx; r_y[i] = ry; r_w[i] = w; r_h[i] = h;
}

void rand_floor(uint8_t i, uint8_t *px, uint8_t *py) __banked
{
    *px = (uint8_t)(r_x[i] + 1 + rn2((uint8_t)(r_w[i] - 2)));
    *py = (uint8_t)(r_y[i] + 1 + rn2((uint8_t)(r_h[i] - 2)));
}

void level_random_floor(uint8_t *px, uint8_t *py) __banked
{
    rand_floor(rn2(rcount), px, py);
}

/* place a trackable floor item (so its pickup can be remembered) */
static void place_item(char ch)
{
    uint8_t i, x, y;
    if (icount >= 8) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;
    lvl[y][x] = ch;
    i_x[icount] = x; i_y[icount] = y; icount++;
}

static int item_at(uint8_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < icount; i++)
        if (i_x[i] == x && i_y[i] == y)
            return i;
    return -1;
}

static int gold_at(uint8_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < gcount; i++)
        if (g_x[i] == x && g_y[i] == y)
            return i;
    return -1;
}

static void place_gold(void)
{
    uint8_t i, x, y;
    if (gcount >= 8) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;
    lvl[y][x] = '$';
    g_x[gcount] = x; g_y[gcount] = y; gcount++;
}

void gen_level(void) __banked
{
    uint8_t i, j;
    uint8_t secw = (uint8_t)((PX1 - PX0 + 1) / SECT_COLS);
    uint8_t sech = (uint8_t)((PY1 - PY0 + 1) / SECT_ROWS);

    rng_set(level_seed(dlvl));     /* deterministic layout for this depth */

    lvl_clear();
    rcount = 0;
    gcount = 0;
    icount = 0;

    for (j = 0; j < SECT_ROWS; j++) {
        for (i = 0; i < SECT_COLS; i++) {
            uint8_t sx0 = (uint8_t)(PX0 + i * secw);
            uint8_t sy0 = (uint8_t)(PY0 + j * sech);
            uint8_t sx1 = (i == SECT_COLS - 1) ? PX1 : (uint8_t)(sx0 + secw - 1);
            uint8_t sy1 = (j == SECT_ROWS - 1) ? PY1 : (uint8_t)(sy0 + sech - 1);
            make_room(rcount, sx0, sy0, sx1, sy1);
            rcount++;
        }
    }

    /* Connect grid-adjacent rooms so every door leads to a real neighbour and
     * corridors stay short (no long diagonal runs across the whole map):
     * chain each row horizontally, then link the rows with one vertical hop. */
    for (j = 0; j < SECT_ROWS; j++)
        for (i = 0; i + 1 < SECT_COLS; i++)
            connect_rooms((uint8_t)(j * SECT_COLS + i),
                          (uint8_t)(j * SECT_COLS + i + 1));
    for (j = 0; j + 1 < SECT_ROWS; j++)
        connect_rooms((uint8_t)(j * SECT_COLS),
                      (uint8_t)((j + 1) * SECT_COLS));
    thin_doors();

    /* stairs in two different random rooms (so their location varies) */
    {
        uint8_t x, y, upr, dnr;
        upr = rn2(rcount);
        do { dnr = rn2(rcount); } while (dnr == upr);
        rand_floor(upr, &x, &y);
        lvl[y][x] = '<';
        up_x = x; up_y = y;
        rand_floor(dnr, &x, &y);
        lvl[y][x] = '>';
        dn_x = x; dn_y = y;
    }

    /* loot (gold piles tracked for persistence; the rest are re-placed each
     * visit since item pickups are not persisted yet) */
    place_gold();
    place_gold();
    place_item('%');      /* food   */
    place_item(')');      /* weapon */
    place_item('[');      /* armor  */
    place_item('!');      /* potion */
    if (dlvl >= 2) place_item('!');
    if (rn2(2))    place_item('?');     /* scroll */
    if (dlvl >= 3 && rn2(2)) place_item('=');   /* ring */

    /* The deepest level holds the Amulet of Yendor instead of a way down:
     * put it on the would-be down-stairs cell (a guaranteed room-floor tile).
     * This needs no RNG, so it can't shift the deterministic gold/item indices
     * or change this level's monster spawns. */
    if (dlvl == DLVL_AMULET)
        lvl[dn_y][dn_x] = has_amulet ? '.' : '"';
}

void apply_gold_persistence(void) __banked
{
    uint8_t b;
    if (dlvl > MAXLVL) return;
    for (b = 0; b < gcount; b++)
        if (gold_taken[dlvl] & (uint8_t)(1u << b))
            lvl[g_y[b]][g_x[b]] = '.';
}

int level_take_gold(uint8_t x, uint8_t y) __banked
{
    int gi = gold_at(x, y);
    lvl[y][x] = '.';
    if (gi >= 0 && dlvl <= MAXLVL)
        gold_taken[dlvl] |= (uint8_t)(1u << gi);
    return gi >= 0;
}

void apply_item_persistence(void) __banked
{
    uint8_t b;
    if (dlvl > MAXLVL) return;
    for (b = 0; b < icount; b++)
        if (item_taken[dlvl] & (uint8_t)(1u << b))
            lvl[i_y[b]][i_x[b]] = '.';
}

int level_take_item(uint8_t x, uint8_t y) __banked
{
    int ii = item_at(x, y);
    lvl[y][x] = '.';
    if (ii >= 0 && dlvl <= MAXLVL)
        item_taken[dlvl] |= (uint8_t)(1u << ii);
    return ii >= 0;
}

void level_reset_persistence(void) __banked
{
    uint8_t i;
    for (i = 0; i <= MAXLVL; i++) {
        gold_taken[i] = 0;
        item_taken[i] = 0;
    }
}
