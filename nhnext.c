/* ============================================================
 * NetHack Next - Phase 2: procedural level generation
 * ------------------------------------------------------------
 * Renders on the ZX Spectrum Next hardware TILEMAP (80x32) and now
 * builds a random NetHack-style dungeon each level: rooms laid out
 * on a grid of sectors, joined by corridors with doors, plus up/down
 * stairs, gold, food and a couple of (static) monsters. Descending
 * the '>' stairs generates the next level.
 *
 * Memory map (Bank 5, fixed 0x4000-0x7FFF, free since code is 0x8000+):
 *   0x4000  tile definitions (128 tiles x 32 bytes, 4bpp 8x8)
 *   0x6000  tilemap (80x32 entries x 2 bytes: tile id + attribute)
 *
 * Still hand-rolled, not the real NetHack engine yet; this is the
 * level-generation foundation that later phases will align with
 * NetHack's mklev.c / monsters / items.
 * ============================================================ */

#include <stdint.h>
#include <arch/zxn.h>     /* ZXN_WRITE_REG / ZXN_READ_REG, zx_border */
#include <input.h>        /* in_inkey, in_wait_nokey                 */

/* ============================================================
 * Colour offsets (0..15). Low 3 bits = hue, +8 = bright.
 * ============================================================ */
#define C_BLACK   0
#define C_BLUE    1
#define C_RED     2
#define C_MAGENTA 3
#define C_GREEN   4
#define C_CYAN    5
#define C_YELLOW  6
#define C_WHITE   7
#define C_BRIGHT  8

/* ============================================================
 * Tilemap hardware layout
 * ============================================================ */
#define TM_W          80
#define TM_H          32
#define TILEDEF_BASE  0x4000u
#define TILEMAP_BASE  0x6000u
#define ROM_FONT      0x3C00u

/* Text ink colours, indexed by colour offset 1..15 (offset 0 is reserved
 * for the graphic-tile master palette below). */
static const uint8_t inkcol[16] = {
    0x00, 0x02, 0x80, 0x82, 0x10, 0x12, 0x90, 0x92,  /* dim    */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF   /* bright */
};

/* ============================================================
 * GRAPHIC TILES (8x8 pixel art) - replace the ASCII map glyphs.
 * Each map cell is one of these tiles, drawn with palette offset 0
 * so its pixels index the 16-colour master palette directly.
 * Text keeps using the ROM font tiles (offsets 1..15).
 * ============================================================ */

/* 16-colour master palette (RRRGGGBB), used by the graphic tiles. */
static const uint8_t master[16] = {
    0x00, 0x49, 0x92, 0xDB, 0xFF,   /* 0 black 1 dkgrey 2 grey 3 ltgrey 4 white */
    0x44, 0x88, 0xD5,               /* 5 dkbrown 6 brown 7 tan                  */
    0xE0, 0x1C, 0x08, 0x03,         /* 8 red 9 green 10 dkgreen 11 blue         */
    0x1F, 0xFC, 0xF0, 0xF9          /* 12 cyan 13 yellow 14 orange 15 skin      */
};

/* tile numbers for graphic tiles (start past the 128 font tiles) */
#define T_ROCK   128
#define T_FLOOR  129
#define T_WALL   130
#define T_CORR   131
#define T_DOOR   132
#define T_SUP    133
#define T_SDOWN  134
#define T_HERO   135
#define T_DOG    136
#define T_RAT    137
#define T_GOLD   138
#define T_FOOD   139
#define T_DOLLAR 140

/* Each tile: 64 pixels (8 rows x 8 cols), values are master-palette indices. */
static const uint8_t gfx[13][64] = {
  { /* T_ROCK */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_FLOOR */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,1,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_WALL (brick) */
    3,3,3,3,3,3,3,1, 3,2,2,2,2,2,2,1, 3,2,2,2,2,2,2,1, 1,1,1,1,1,1,1,1,
    3,3,3,1,3,3,3,3, 2,2,2,1,2,2,2,2, 2,2,2,1,2,2,2,2, 1,1,1,1,1,1,1,1 },
  { /* T_CORR (speckled) */
    0,0,0,0,0,0,0,0, 0,0,1,0,0,0,0,0, 0,0,0,0,0,1,0,0, 0,0,0,0,0,0,0,0,
    0,1,0,0,0,0,0,0, 0,0,0,0,1,0,0,0, 0,0,0,0,0,0,0,0, 0,0,1,0,0,0,0,0 },
  { /* T_DOOR */
    2,2,2,2,2,2,2,2, 2,6,6,6,6,6,6,2, 2,6,7,6,6,7,6,2, 2,6,6,6,6,6,6,2,
    2,6,6,6,13,6,6,2, 2,6,7,6,6,7,6,2, 2,6,6,6,6,6,6,2, 2,2,2,2,2,2,2,2 },
  { /* T_SUP (stairs up) */
    0,0,0,0,0,0,3,3, 0,0,0,0,0,3,3,2, 0,0,0,0,3,3,2,0, 0,0,0,3,3,2,0,0,
    0,0,3,3,2,0,0,0, 0,3,3,2,0,0,0,0, 3,3,2,0,0,0,0,0, 3,3,3,3,3,3,3,3 },
  { /* T_SDOWN (stairs down) */
    3,3,3,3,3,3,3,3, 0,0,0,0,0,2,3,3, 0,0,0,0,2,3,3,0, 0,0,0,2,3,3,0,0,
    0,0,2,3,3,0,0,0, 0,2,3,3,0,0,0,0, 2,3,3,0,0,0,0,0, 3,3,0,0,0,0,0,0 },
  { /* T_HERO */
    0,0,0,15,15,0,0,0, 0,0,0,15,15,0,0,0, 0,0,15,15,15,15,0,0, 0,0,0,11,11,0,0,0,
    0,11,11,11,11,11,11,0, 0,0,11,11,11,11,0,0, 0,0,11,0,0,11,0,0, 0,0,11,0,0,11,0,0 },
  { /* T_DOG */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,6,6, 0,6,6,6,6,6,6,0, 6,6,6,6,6,6,6,0,
    6,6,6,6,6,6,0,0, 0,6,0,6,0,6,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_RAT */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,8, 0,2,2,2,2,2,8,0,
    2,2,2,2,2,2,2,0, 0,2,0,2,0,2,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_GOLD */
    0,0,0,0,0,0,0,0, 0,0,13,13,13,0,0,0, 0,13,14,14,14,13,0,0, 0,13,14,13,14,13,0,0,
    0,13,14,14,14,13,0,0, 0,0,13,13,13,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_FOOD (drumstick) */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,4,0, 0,0,0,0,0,4,4,0, 0,0,0,0,6,6,0,0,
    0,0,0,6,6,0,0,0, 0,0,8,8,6,0,0,0, 0,8,8,8,0,0,0,0, 0,0,8,0,0,0,0,0 },
  { /* T_DOLLAR (green '$', matches the status text colour) */
    0,0,0,9,0,0,0,0, 0,9,9,9,9,9,0,0, 0,9,0,9,0,0,0,0, 0,9,9,9,9,9,0,0,
    0,0,0,9,0,9,0,0, 0,9,9,9,9,9,0,0, 0,0,0,9,0,0,0,0, 0,0,0,0,0,0,0,0 }
};

/* pack 64 palette indices into a 4bpp 8x8 tile (32 bytes) at tile slot */
static void pack_tile(uint8_t tilenum, const uint8_t *px)
{
    uint8_t *dst = (uint8_t *)(TILEDEF_BASE + (uint16_t)tilenum * 32);
    uint8_t i;
    for (i = 0; i < 32; i++)
        dst[i] = (uint8_t)(((px[i * 2] & 0x0F) << 4) | (px[i * 2 + 1] & 0x0F));
}

static void load_gfx_tiles(void)
{
    uint8_t i;
    for (i = 0; i < 13; i++)
        pack_tile((uint8_t)(T_ROCK + i), gfx[i]);
}

static void tm_init_font(void)
{
    const uint8_t *src = (const uint8_t *)ROM_FONT;
    uint8_t *dst = (uint8_t *)TILEDEF_BASE;
    uint16_t c;
    uint8_t row, col, b;

    for (c = 0; c < 128; c++) {
        const uint8_t *g = src + (c << 3);
        for (row = 0; row < 8; row++) {
            b = g[row];
            for (col = 0; col < 4; col++) {        /* 2 pixels per byte */
                uint8_t left  = (b & 0x80) ? 1 : 0; b <<= 1;
                uint8_t right = (b & 0x80) ? 1 : 0; b <<= 1;
                *dst++ = (uint8_t)((left << 4) | right);
            }
        }
    }
}

static void tm_init_palette(void)
{
    uint8_t i, o;

    /* reg 0x43: bits 5-4 = layer (11 = tilemap), bit 6 = first/second.
     * Tilemap first palette = 0b0011_0000 = 0x30 (autoinc on). */
    ZXN_WRITE_REG(0x43, 0x30);

    /* indices 0..15: master palette for the graphic tiles (offset 0) */
    ZXN_WRITE_REG(0x40, 0x00);
    for (i = 0; i < 16; i++)
        ZXN_WRITE_REG(0x41, master[i]);

    /* offsets 1..15: black paper + ink colour, for coloured text */
    for (o = 1; o < 16; o++) {
        ZXN_WRITE_REG(0x40, (uint8_t)(o << 4));   /* index o*16        */
        ZXN_WRITE_REG(0x41, 0x00);                /* o*16+0 : black    */
        ZXN_WRITE_REG(0x41, inkcol[o]);           /* o*16+1 : ink      */
    }
}

static void tm_init(void)
{
    ZXN_WRITE_REG(0x07, 0x03);   /* 28 MHz turbo for snappy redraws         */
    ZXN_WRITE_REG(0x68, 0x80);   /* disable ULA layer: only tilemap is shown*/
    ZXN_WRITE_REG(0x4A, 0x00);   /* fallback colour = black (border area)   */
    ZXN_WRITE_REG(0x6F, 0x00);   /* tile definitions base -> 0x4000         */
    ZXN_WRITE_REG(0x6E, 0x20);   /* tilemap base          -> 0x6000         */
    ZXN_WRITE_REG(0x4C, 0x0F);   /* tilemap transparency index              */
    ZXN_WRITE_REG(0x6C, 0x00);   /* default attribute                       */

    tm_init_font();
    load_gfx_tiles();
    tm_init_palette();

    ZXN_WRITE_REG(0x6B, 0xC0);   /* enable: bit7 on, bit6 80x32, attrs kept */
}

/* ============================================================
 * DISPLAY
 * ============================================================ */

static void putcell(uint8_t x, uint8_t y, uint8_t ch, uint8_t coff)
{
    uint8_t *p = (uint8_t *)(TILEMAP_BASE + (((uint16_t)y * TM_W + x) << 1));
    p[0] = ch;
    p[1] = (uint8_t)(coff << 4);
}

/* draw a graphic tile (palette offset 0 -> master palette) */
static void puttile(uint8_t x, uint8_t y, uint8_t tile)
{
    uint8_t *p = (uint8_t *)(TILEMAP_BASE + (((uint16_t)y * TM_W + x) << 1));
    p[0] = tile;
    p[1] = 0;
}

static uint8_t tile_for(char c)
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
    case 'd': return T_DOG;
    case 'r': return T_RAT;
    default:  return T_ROCK;
    }
}

static uint8_t print_str(uint8_t x, uint8_t y, const char *s, uint8_t coff)
{
    while (*s && x < TM_W) {
        putcell(x++, y, (uint8_t)*s, coff);
        s++;
    }
    return x;
}

static uint8_t put_uint(uint8_t x, uint8_t y, uint16_t v, uint8_t coff)
{
    char t[5];
    uint8_t n = 0;

    if (v == 0) {
        putcell(x++, y, '0', coff);
        return x;
    }
    while (v) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) putcell(x++, y, (uint8_t)t[--n], coff);
    return x;
}

static void clear_line(uint8_t y, uint8_t coff)
{
    uint8_t x;
    for (x = 0; x < TM_W; x++)
        putcell(x, y, ' ', coff);
}

static void tm_cls(void)
{
    uint8_t y;
    for (y = 0; y < TM_H; y++)
        clear_line(y, C_BLACK);
}

/* ============================================================
 * INPUT
 * ============================================================ */

/* Returns the key currently held (does NOT wait for release), so holding a
 * movement key keeps moving. The main loop throttles the repeat rate. */
static int getkey(void)
{
    int k;
    do {
        k = in_inkey();
    } while (k == 0);
    return k;
}

/* ============================================================
 * RNG - small xorshift16, seeded from raster + FRAMES for variety
 * ============================================================ */

static uint16_t rng = 1;
static uint16_t world_seed = 1;     /* fixed per game; levels derive from it */

static void rng_seed(void)
{
    uint16_t s = (uint16_t)ZXN_READ_REG(0x1F);       /* raster line low  */
    s = (uint16_t)((s << 8) ^ (uint16_t)ZXN_READ_REG(0x1E));
    s ^= *(volatile uint16_t *)0x5C78u;              /* FRAMES sysvar    */
    world_seed = s ? s : 0xACE1u;
    rng = world_seed;
}

static uint16_t rng_next(void)
{
    rng ^= (uint16_t)(rng << 7);
    rng ^= (uint16_t)(rng >> 9);
    rng ^= (uint16_t)(rng << 8);
    return rng;
}

static uint8_t rn2(uint8_t n)        /* 0 .. n-1 */
{
    return n ? (uint8_t)(rng_next() % n) : 0;
}

/* ============================================================
 * LEVEL - terrain buffer and procedural generator
 *   '-' '|' wall   '.' floor   '#' corridor   '+' door
 *   '>' '<' stairs   '$' gold   '%' food   'd' dog   'r' rat
 *   ' ' rock (impassable)
 * ============================================================ */

#define MAPW 80
#define MAPH 21
#define OX 0              /* map -> screen column offset */
#define OY 1              /* map -> screen row offset (row 0 = messages) */

/* playable area (leaves a rock margin around the edges) */
#define PX0 1
#define PY0 1
#define PX1 78
#define PY1 19

#define SECT_COLS 4
#define SECT_ROWS 2
#define MAXROOMS  (SECT_COLS * SECT_ROWS)

static char lvl[MAPH][MAPW];

static uint8_t rcount;
static uint8_t r_x[MAXROOMS], r_y[MAXROOMS], r_w[MAXROOMS], r_h[MAXROOMS];

static char terrain(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH)
        return ' ';
    return lvl[y][x];
}

static int walkable(char c)
{
    return !(c == '|' || c == '-' || c == ' ');
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
    /* floor/door/corridor left unchanged */
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

/* random interior floor cell of room i */
static void rand_floor(uint8_t i, uint8_t *px, uint8_t *py)
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

static int hero_x, hero_y;
static uint16_t dlvl = 1;
static uint16_t turns = 0;

/* player */
static uint8_t  php = 12, pmaxhp = 12;
static uint16_t gold = 0;
static uint8_t  dead = 0;
static uint8_t  acted = 0;       /* did the player's action consume a turn? */

/* monsters (kept in arrays, not in the terrain buffer, so they can move
 * and carry HP independently of what is drawn) */
#define MAXMON 8
static uint8_t m_x[MAXMON], m_y[MAXMON], m_hp[MAXMON], m_alive[MAXMON];
static char    m_type[MAXMON];
static uint8_t mcount;

/* per-level positions (set during generation, used for placement/persistence) */
static uint8_t up_x, up_y, dn_x, dn_y;   /* this level's stair positions */
static uint8_t gcount;
static uint8_t g_x[8], g_y[8];           /* gold pile positions          */

static int monster_at(int x, int y)
{
    uint8_t i;
    for (i = 0; i < mcount; i++)
        if (m_alive[i] && m_x[i] == x && m_y[i] == y)
            return i;
    return -1;
}

static const char *mon_name(char t)
{
    return (t == 'd') ? "dog" : "rat";
}

static void spawn_monster(char type, uint8_t hp)
{
    uint8_t i, x, y;
    if (mcount >= MAXMON) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;                 /* floor only */
    if (x == up_x && y == up_y) return;           /* keep the start clear */
    if (monster_at(x, y) >= 0) return;
    m_x[mcount] = x; m_y[mcount] = y;
    m_hp[mcount] = hp; m_type[mcount] = type; m_alive[mcount] = 1;
    mcount++;
}

/* ---- level persistence --------------------------------------------------
 * Layout is regenerated deterministically from a per-depth seed, so the
 * same Dlvl is always the same map. Player-visible mutations (gold taken,
 * monsters killed) are remembered in tiny per-level bitmasks and re-applied
 * after regeneration. (No banking needed; ~50 bytes total.)              */
#define MAXLVL 24
static uint8_t  gold_taken[MAXLVL + 1];   /* bit i: gold pile i collected */
static uint8_t  mon_dead[MAXLVL + 1];     /* bit i: monster i killed      */

static uint16_t level_seed(uint16_t d)
{
    return (uint16_t)(world_seed + (uint16_t)(d * 0x9E37u));
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

static void gen_level(void)
{
    uint8_t i, j, k;
    uint8_t secw = (uint8_t)((PX1 - PX0 + 1) / SECT_COLS);
    uint8_t sech = (uint8_t)((PY1 - PY0 + 1) / SECT_ROWS);
    uint8_t cx0, cy0, cx1, cy1;

    rng = level_seed(dlvl);     /* deterministic layout for this depth */

    lvl_clear();
    rcount = 0;
    mcount = 0;
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

    /* loot */
    place_gold();
    place_gold();
    place_thing('%');

    /* monsters (more of them the deeper you go) */
    spawn_monster('r', 3);
    spawn_monster('r', 3);
    spawn_monster('d', 6);
    if (dlvl >= 3) spawn_monster('r', 3);
    if (dlvl >= 5) spawn_monster('d', 6);

    /* re-apply remembered mutations for this depth */
    if (dlvl <= MAXLVL) {
        uint8_t b;
        for (b = 0; b < gcount; b++)
            if (gold_taken[dlvl] & (uint8_t)(1u << b))
                lvl[g_y[b]][g_x[b]] = '.';
        for (b = 0; b < mcount; b++)
            if (mon_dead[dlvl] & (uint8_t)(1u << b))
                m_alive[b] = 0;
    }
}

/* ============================================================
 * RENDER
 * ============================================================ */

static void draw_map(void)
{
    uint8_t x, y, t;
    int mi;

    /* Single pass: every cell is written exactly once with its final
     * tile (hero > monster > terrain). Drawing terrain first and then
     * overwriting with monsters caused a one-frame flicker. */
    for (y = 0; y < MAPH; y++) {
        for (x = 0; x < MAPW; x++) {
            if (x == (uint8_t)hero_x && y == (uint8_t)hero_y)
                t = T_HERO;
            else if ((mi = monster_at(x, y)) >= 0)
                t = (uint8_t)(m_type[mi] == 'd' ? T_DOG : T_RAT);
            else
                t = tile_for(lvl[y][x]);
            puttile((uint8_t)(OX + x), (uint8_t)(OY + y), t);
        }
    }
}

static void msg(const char *s)
{
    clear_line(0, C_WHITE);
    print_str(0, 0, s, C_WHITE | C_BRIGHT);
}

/* message built from prefix + a name + suffix, e.g. "You kill the rat!" */
static void msg2(const char *a, const char *b, const char *c)
{
    uint8_t x;
    clear_line(0, C_WHITE);
    x = print_str(0, 0, a, C_WHITE | C_BRIGHT);
    x = print_str(x, 0, b, C_WHITE | C_BRIGHT);
    print_str(x, 0, c, C_WHITE | C_BRIGHT);
}

/* message with an embedded number, e.g. "You pick up 7 gold pieces." */
static void msg_num(const char *a, uint16_t n, const char *c)
{
    uint8_t x;
    clear_line(0, C_WHITE);
    x = print_str(0, 0, a, C_WHITE | C_BRIGHT);
    x = put_uint(x, 0, n, C_WHITE | C_BRIGHT);
    print_str(x, 0, c, C_WHITE | C_BRIGHT);
}

static void draw_help(void)
{
    print_str(0, 25,
        "Move: WASD or arrows    Diagonals: Q E Z C    Enter: use stairs    . wait",
        C_CYAN | C_BRIGHT);
}

static void draw_status(void)
{
    uint8_t x;
    print_str(0, 22,
        "Player the Tourist      St:14 Dx:11 Co:14 In:10 Wi:8 Ch:10   Lawful",
        C_GREEN | C_BRIGHT);

    clear_line(23, C_GREEN);
    x = print_str(0, 23, "Dlvl:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, dlvl, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  ", C_GREEN | C_BRIGHT);
    puttile(x, 23, T_DOLLAR); x++;        /* green '$' tile (ROM '$' glyph is blank) */
    x = print_str(x, 23, ":", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, gold, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  HP:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, php, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "(", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, pmaxhp, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, ")  Pw:2(2)  AC:10  Xp:1/0  T:", C_GREEN | C_BRIGHT);
    put_uint(x, 23, turns, C_GREEN | C_BRIGHT);
}

/* ============================================================
 * GAME
 * ============================================================ */

static void describe(char dest, int moved)
{
    if (!moved) {
        if (dest == ' ')      msg("It's solid stone.");
        else                  msg("It's a wall.");
        return;
    }
    switch (dest) {
    case '>': msg("There is a staircase down here."); break;
    case '<': msg("There is a staircase up here.");   break;
    case '%': msg("There is a slice of food here.");  break;
    default:  msg("");                                break;
    }
}

/* hero attacks monster mi (the monster strikes back on its own turn) */
static void attack_monster(uint8_t mi)
{
    uint8_t dmg = (uint8_t)(rn2(4) + 1);    /* 1..4 */
    const char *name = mon_name(m_type[mi]);

    turns++;
    if (m_hp[mi] <= dmg) {
        m_alive[mi] = 0;
        if (dlvl <= MAXLVL)
            mon_dead[dlvl] |= (uint8_t)(1u << mi);   /* remember the kill */
        msg2("You kill the ", name, "!");
    } else {
        m_hp[mi] = (uint8_t)(m_hp[mi] - dmg);
        msg2("You hit the ", name, ".");
    }
}

/* ---- monster turn: chase the hero, attack when adjacent ---- */

static int iabs(int v) { return v < 0 ? -v : v; }

static void monster_hits_player(uint8_t i)
{
    uint8_t bite = (uint8_t)(rn2(3) + 1);   /* 1..3 */
    const char *name = mon_name(m_type[i]);
    if (php <= bite) {
        php = 0; dead = 1;
        msg2("The ", name, " kills you!");
    } else {
        php = (uint8_t)(php - bite);
        msg2("The ", name, " bites you!");
    }
}

static int try_mon_move(uint8_t i, int dx, int dy)
{
    int nx = (int)m_x[i] + dx;
    int ny = (int)m_y[i] + dy;
    if (!walkable(terrain(nx, ny))) return 0;
    if (nx == hero_x && ny == hero_y) return 0;   /* hero handled by attack */
    if (monster_at(nx, ny) >= 0) return 0;        /* another monster        */
    m_x[i] = (uint8_t)nx; m_y[i] = (uint8_t)ny;
    return 1;
}

static void mon_step(uint8_t i)
{
    int ddx = hero_x - (int)m_x[i];
    int ddy = hero_y - (int)m_y[i];
    int dx = (ddx > 0) ? 1 : (ddx < 0) ? -1 : 0;
    int dy = (ddy > 0) ? 1 : (ddy < 0) ? -1 : 0;

    if (iabs(ddx) <= 1 && iabs(ddy) <= 1) {   /* adjacent -> attack */
        monster_hits_player(i);
        return;
    }
    /* greedy chase: diagonal first, then slide along walls */
    if (try_mon_move(i, dx, dy)) return;
    if (dx && try_mon_move(i, dx, 0)) return;
    if (dy && try_mon_move(i, 0, dy)) return;
}

static void monsters_turn(void)
{
    uint8_t i;
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i]) continue;
        mon_step(i);
        if (dead) return;
    }
}

static void try_move(int dx, int dy)
{
    int nx = hero_x + dx;
    int ny = hero_y + dy;
    char dest = terrain(nx, ny);
    int mi;

    if (!walkable(dest)) {
        describe(dest, 0);
        return;
    }
    mi = monster_at(nx, ny);
    if (mi >= 0) {
        acted = 1;
        attack_monster((uint8_t)mi);
        return;
    }
    hero_x = nx;
    hero_y = ny;
    turns++;
    acted = 1;
    if (dest == '$') {
        int gi = gold_at((uint8_t)nx, (uint8_t)ny);
        uint16_t amt = (uint16_t)(rn2(20) + 1);
        gold = (uint16_t)(gold + amt);
        lvl[ny][nx] = '.';
        if (gi >= 0 && dlvl <= MAXLVL)
            gold_taken[dlvl] |= (uint8_t)(1u << gi);   /* remember the pickup */
        msg_num("You pick up ", amt, " gold pieces.");
    } else {
        describe(dest, 1);
    }
}

static void go_down(void)
{
    if (terrain(hero_x, hero_y) == '>') {
        dlvl++;
        turns++;
        gen_level();
        hero_x = up_x; hero_y = up_y;     /* arrive on the new up-stairs */
        msg("You descend the stairs.");
    } else {
        msg("You can't go down here.");
    }
}

static void go_up(void)
{
    if (terrain(hero_x, hero_y) == '<') {
        if (dlvl > 1) {
            dlvl--;
            turns++;
            gen_level();
            hero_x = dn_x; hero_y = dn_y; /* arrive on the new down-stairs */
            msg("You climb up the stairs.");
        } else {
            msg("You can't go up from here.");
        }
    } else {
        msg("You can't go up here.");
    }
}

static void new_game(void)
{
    uint8_t i;

    php = pmaxhp;
    gold = 0;
    dlvl = 1;
    turns = 0;
    dead = 0;
    for (i = 0; i <= MAXLVL; i++) { gold_taken[i] = 0; mon_dead[i] = 0; }
    rng_seed();                  /* a brand new world */
    gen_level();
    hero_x = up_x; hero_y = up_y;
}

/* Title screen. The seed comes from how long the player takes to press a
 * key, which gives far more variety than reading the machine state at the
 * same instant every cold boot. */
static void title_screen(void)
{
    uint16_t s = 1;

    tm_cls();
    print_str(34,  8, "NetHack Next", C_YELLOW | C_BRIGHT);
    print_str(22, 10, "A roguelike for the ZX Spectrum Next", C_CYAN | C_BRIGHT);
    print_str(27, 14, "Press any key to begin...", C_WHITE | C_BRIGHT);

    while (in_inkey() == 0)
        s += 0x9E37u;
    s ^= (uint16_t)(((uint16_t)ZXN_READ_REG(0x1F) << 8) ^ ZXN_READ_REG(0x1E));
    world_seed = s ? s : 0xACE1u;
    rng = world_seed;
    in_wait_nokey();
}

void main(void)
{
    int k;

    zx_border(C_BLACK);
    tm_init();

    title_screen();
    gen_level();
    hero_x = up_x; hero_y = up_y;

    tm_cls();
    draw_help();
    draw_status();
    draw_map();
    msg("Welcome to NetHack Next!");

    for (;;) {
        k = getkey();
        if (k >= 'A' && k <= 'Z')
            k += 32;                 /* normalise letters */

        acted = 0;
        switch (k) {
        /* orthogonal: WASD, vi hjkl, cursor keys (8/9/10/11) */
        case 'w': case 'k': case 11: try_move( 0, -1); break;
        case 's': case 'j': case 10: try_move( 0, +1); break;
        case 'a': case 'h': case  8: try_move(-1,  0); break;
        case 'd': case 'l': case  9: try_move(+1,  0); break;
        /* diagonals: QEZC and vi yubn */
        case 'q': case 'y': try_move(-1, -1); break;
        case 'e': case 'u': try_move(+1, -1); break;
        case 'z': case 'b': try_move(-1, +1); break;
        case 'c': case 'n': try_move(+1, +1); break;
        /* Enter: use whichever stair you stand on (debounced below) */
        case 13:
            if (terrain(hero_x, hero_y) == '>')      go_down();
            else if (terrain(hero_x, hero_y) == '<') go_up();
            else msg("There are no stairs here.");
            in_wait_nokey();          /* don't repeat-trigger stairs */
            break;
        /* wait */
        case '.':
        case ' ': turns++; acted = 1; msg("You wait."); break;
        default:  break;
        }

        if (acted && !dead)
            monsters_turn();        /* monsters chase and attack */

        draw_status();
        draw_map();

        if (dead) {
            msg("You die...   Press Enter to start over.");
            in_wait_nokey();
            do { k = getkey(); } while (k != 13);
            new_game();
            draw_status();
            draw_map();
            msg("You feel much better.  A new adventure begins!");
            in_wait_nokey();
        }

        in_pause(70);   /* throttle continuous (held-key) movement */
    }
}
