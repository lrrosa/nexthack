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

static const uint8_t inkcol[16] = {
    0x00, 0x02, 0x80, 0x82, 0x10, 0x12, 0x90, 0x92,  /* dim    */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF   /* bright */
};

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
    uint8_t o;

    /* reg 0x43: bits 5-4 = layer (11 = tilemap), bit 6 = first/second.
     * Tilemap first palette = 0b0011_0000 = 0x30 (autoinc on). */
    ZXN_WRITE_REG(0x43, 0x30);
    for (o = 0; o < 16; o++) {
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

static int getkey(void)
{
    int k;
    do {
        k = in_inkey();
    } while (k == 0);
    in_wait_nokey();
    return k;
}

/* ============================================================
 * RNG - small xorshift16, seeded from raster + FRAMES for variety
 * ============================================================ */

static uint16_t rng = 1;

static void rng_seed(void)
{
    uint16_t s = (uint16_t)ZXN_READ_REG(0x1F);       /* raster line low  */
    s = (uint16_t)((s << 8) ^ (uint16_t)ZXN_READ_REG(0x1E));
    s ^= *(volatile uint16_t *)0x5C78u;              /* FRAMES sysvar    */
    rng = s ? s : 0xACE1u;
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

static void gen_level(void)
{
    uint8_t i, j, k;
    uint8_t secw = (uint8_t)((PX1 - PX0 + 1) / SECT_COLS);
    uint8_t sech = (uint8_t)((PY1 - PY0 + 1) / SECT_ROWS);
    uint8_t cx0, cy0, cx1, cy1;

    lvl_clear();
    rcount = 0;

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

    /* stairs: up in first room (hero starts here), down in last room */
    {
        uint8_t x, y;
        rand_floor(0, &x, &y);
        lvl[y][x] = '<';
        hero_x = x; hero_y = y;
        rand_floor((uint8_t)(rcount - 1), &x, &y);
        lvl[y][x] = '>';
    }

    /* a little loot and life */
    place_thing('$');
    place_thing('$');
    place_thing('%');
    place_thing('d');
    place_thing('r');
}

/* ============================================================
 * RENDER
 * ============================================================ */

static uint8_t colour_for(char c)
{
    switch (c) {
    case '@': return C_YELLOW | C_BRIGHT;
    case '-':
    case '|': return C_WHITE | C_BRIGHT;
    case '+': return C_YELLOW;
    case '#':
    case '.': return C_WHITE;
    case '$': return C_YELLOW | C_BRIGHT;
    case '%': return C_RED | C_BRIGHT;
    case 'd': return C_WHITE | C_BRIGHT;
    case 'r': return C_RED;
    case '<':
    case '>': return C_WHITE | C_BRIGHT;
    default:  return C_BLACK;        /* rock: invisible */
    }
}

static void draw_map(void)
{
    uint8_t x, y;
    for (y = 0; y < MAPH; y++) {
        for (x = 0; x < MAPW; x++) {
            char c = lvl[y][x];
            putcell((uint8_t)(OX + x), (uint8_t)(OY + y),
                    (uint8_t)c, colour_for(c));
        }
    }
    putcell((uint8_t)(OX + hero_x), (uint8_t)(OY + hero_y),
            '@', colour_for('@'));
}

static void msg(const char *s)
{
    clear_line(0, C_WHITE);
    print_str(0, 0, s, C_WHITE | C_BRIGHT);
}

static void draw_help(void)
{
    print_str(0, 25,
        "Move: WASD or arrows   Diagonals: Q E Z C   > down  < up   . wait",
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
    x = print_str(x, 23, "  $:0  HP:12(12)  Pw:2(2)  AC:10  Xp:1/0  T:",
                  C_GREEN | C_BRIGHT);
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
    case '$': msg("You see some gold here.");         break;
    case '%': msg("There is a slice of food here.");  break;
    case 'd': msg("You see a little dog here.");      break;
    case 'r': msg("You see a giant rat here.");       break;
    default:  msg("");                                break;
    }
}

static void try_move(int dx, int dy)
{
    int nx = hero_x + dx;
    int ny = hero_y + dy;
    char dest = terrain(nx, ny);

    if (walkable(dest)) {
        hero_x = nx;
        hero_y = ny;
        turns++;
        describe(dest, 1);
    } else {
        describe(dest, 0);
    }
}

static void go_down(void)
{
    if (terrain(hero_x, hero_y) == '>') {
        dlvl++;
        turns++;
        gen_level();
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
            msg("You climb up the stairs.");
        } else {
            msg("You can't go up from here.");
        }
    } else {
        msg("You can't go up here.");
    }
}

void main(void)
{
    int k;

    zx_border(C_BLACK);
    tm_init();
    tm_cls();
    rng_seed();
    gen_level();

    draw_help();
    draw_status();
    draw_map();
    msg("Welcome to NetHack Next!");

    for (;;) {
        k = getkey();
        if (k >= 'A' && k <= 'Z')
            k += 32;                 /* normalise letters */

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
        /* stairs */
        case '>': go_down(); break;
        case '<': go_up();   break;
        /* wait */
        case '.':
        case ' ': turns++; msg("You wait."); break;
        default:  break;
        }
        draw_status();
        draw_map();
    }
}
