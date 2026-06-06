/* level.c - terrain buffer, procedural dungeon generation and per-level
 * persistence (deterministic layout from a per-depth seed + remembered gold). */

#include "level.h"
#include "platform.h"     /* T_* tile numbers          */
#include "rng.h"          /* rng_set, rn2, world_seed  */
#include "game.h"         /* dlvl, MAXLVL              */

/* playable area (leaves a rock margin around the edges) */
#define PX0 1
#define PY0 1
#define PX1 78
#define PY1 19

#define SECT_COLS 4
#define SECT_ROWS 2
#define MAXROOMS  (SECT_COLS * SECT_ROWS)

char    lvl[MAPH][MAPW];
uint8_t rcount;
uint8_t up_x, up_y, dn_x, dn_y;

static uint8_t r_x[MAXROOMS], r_y[MAXROOMS], r_w[MAXROOMS], r_h[MAXROOMS];

/* gold piles, for indexing into the persistence bitmask */
static uint8_t gcount;
static uint8_t g_x[8], g_y[8];
static uint8_t gold_taken[MAXLVL + 1];    /* bit i: gold pile i collected */

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
    default:  return T_ROCK;
    }
}

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

void rand_floor(uint8_t i, uint8_t *px, uint8_t *py)
{
    *px = (uint8_t)(r_x[i] + 1 + rn2((uint8_t)(r_w[i] - 2)));
    *py = (uint8_t)(r_y[i] + 1 + rn2((uint8_t)(r_h[i] - 2)));
}

static void place_thing(char ch)
{
    uint8_t i = rn2(rcount);
    uint8_t x, y;
    rand_floor(i, &x, &y);
    if (lvl[y][x] == '.')           /* don't overwrite stairs/other things */
        lvl[y][x] = ch;
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

void gen_level(void)
{
    uint8_t i, j, k;
    uint8_t secw = (uint8_t)((PX1 - PX0 + 1) / SECT_COLS);
    uint8_t sech = (uint8_t)((PY1 - PY0 + 1) / SECT_ROWS);
    uint8_t cx0, cy0, cx1, cy1;

    rng_set(level_seed(dlvl));     /* deterministic layout for this depth */

    lvl_clear();
    rcount = 0;
    gcount = 0;

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

    /* connect rooms in scan order: guarantees full connectivity */
    for (k = 0; k + 1 < rcount; k++) {
        cx0 = (uint8_t)(r_x[k] + r_w[k] / 2);
        cy0 = (uint8_t)(r_y[k] + r_h[k] / 2);
        cx1 = (uint8_t)(r_x[k + 1] + r_w[k + 1] / 2);
        cy1 = (uint8_t)(r_y[k + 1] + r_h[k + 1] / 2);
        dig_corridor(cx0, cy0, cx1, cy1);
    }

    /* stairs: up in the first room, down in the last room */
    {
        uint8_t x, y;
        rand_floor(0, &x, &y);
        lvl[y][x] = '<';
        up_x = x; up_y = y;
        rand_floor((uint8_t)(rcount - 1), &x, &y);
        lvl[y][x] = '>';
        dn_x = x; dn_y = y;
    }

    /* loot (gold piles tracked for persistence; food is decorative for now) */
    place_gold();
    place_gold();
    place_thing('%');
}

void apply_gold_persistence(void)
{
    uint8_t b;
    if (dlvl > MAXLVL) return;
    for (b = 0; b < gcount; b++)
        if (gold_taken[dlvl] & (uint8_t)(1u << b))
            lvl[g_y[b]][g_x[b]] = '.';
}

int level_take_gold(uint8_t x, uint8_t y)
{
    int gi = gold_at(x, y);
    lvl[y][x] = '.';
    if (gi >= 0 && dlvl <= MAXLVL)
        gold_taken[dlvl] |= (uint8_t)(1u << gi);
    return gi >= 0;
}

void level_reset_persistence(void)
{
    uint8_t i;
    for (i = 0; i <= MAXLVL; i++)
        gold_taken[i] = 0;
}
