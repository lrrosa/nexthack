/* ============================================================
 * NetHack Next - Phase 1b: 80-column hardware tilemap
 * ------------------------------------------------------------
 * Renders on the ZX Spectrum Next hardware TILEMAP layer (80x32
 * characters), which matches NetHack's native 80-column map. The
 * ULA layer is disabled; the tilemap is the only thing on screen.
 *
 * Memory map used (Bank 5, fixed at 0x4000-0x7FFF, free because the
 * program lives at 0x8000+):
 *   0x4000  tile definitions (128 tiles x 32 bytes, 4bpp 8x8)
 *   0x6000  tilemap (80x32 entries x 2 bytes: tile id + attribute)
 *
 * Glyphs are built at runtime by expanding the 1bpp ROM font at
 * 0x3C00 into 4bpp tiles (pixel value 0 = paper, 1 = ink). Per-cell
 * colour comes from the attribute's palette offset (0..15), mapped
 * through the tilemap palette so offset O selects ink colour O with
 * a black paper.
 *
 * This is still a hand-made demo level, not real NetHack. It is the
 * display foundation for the engine modules to come (mklev, mon...).
 * ============================================================ */

#include <stdint.h>
#include <arch/zxn.h>     /* ZXN_WRITE_REG (Next registers), zx_border */
#include <input.h>        /* in_inkey, in_wait_nokey                   */

/* ============================================================
 * Colour offsets (0..15). Low 3 bits = hue, +8 = bright.
 * These index the tilemap palette set up in tm_init_palette().
 * ============================================================ */
#define C_BLACK   0
#define C_BLUE    1
#define C_RED     2
#define C_MAGENTA 3
#define C_GREEN   4
#define C_CYAN    5
#define C_YELLOW  6
#define C_WHITE   7
#define C_BRIGHT  8       /* add to a hue for the bright variant */

/* ============================================================
 * Tilemap hardware layout
 * ============================================================ */
#define TM_W          80
#define TM_H          32
#define TILEDEF_BASE  0x4000u   /* Bank 5 offset 0x0000 -> reg 0x6F = 0x00 */
#define TILEMAP_BASE  0x6000u   /* Bank 5 offset 0x2000 -> reg 0x6E = 0x20 */
#define ROM_FONT      0x3C00u   /* 1bpp ROM font, glyph c at 0x3C00 + c*8  */

/* 8-bit colours (RRRGGGBB) for ink offsets 0..15 (dim 0..7, bright 8..15) */
static const uint8_t inkcol[16] = {
    0x00, 0x02, 0x80, 0x82, 0x10, 0x12, 0x90, 0x92,  /* dim    */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF   /* bright */
};

/* Expand the 1bpp ROM font into 4bpp tiles (0 = paper, 1 = ink). */
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

/* Tilemap palette: for each offset o, index o*16+0 = black paper,
 * index o*16+1 = ink colour. Glyph pixels (0/1) pick paper/ink. */
static void tm_init_palette(void)
{
    uint8_t o;

    ZXN_WRITE_REG(0x43, 0x60);   /* select tilemap palette (first), autoinc on */
    for (o = 0; o < 16; o++) {
        ZXN_WRITE_REG(0x40, (uint8_t)(o << 4));   /* palette index = o*16   */
        ZXN_WRITE_REG(0x41, 0x00);                /* o*16+0 : paper = black */
        ZXN_WRITE_REG(0x41, inkcol[o]);           /* o*16+1 : ink           */
    }
}

static void tm_init(void)
{
    ZXN_WRITE_REG(0x68, 0x80);   /* disable ULA layer: only tilemap is shown   */
    ZXN_WRITE_REG(0x6F, 0x00);   /* tile definitions base -> 0x4000            */
    ZXN_WRITE_REG(0x6E, 0x20);   /* tilemap base          -> 0x6000            */
    ZXN_WRITE_REG(0x4C, 0x0F);   /* tilemap transparency index (glyphs use 0/1)*/
    ZXN_WRITE_REG(0x6C, 0x00);   /* default attribute (per-cell attrs used)    */

    tm_init_font();
    tm_init_palette();

    /* enable last: bit7 on, bit6 80x32, bit5 off (keep attribute byte) */
    ZXN_WRITE_REG(0x6B, 0xC0);
}

/* ============================================================
 * DISPLAY - write characters into the tilemap
 * ============================================================ */

/* one cell: tile id = ASCII code, attribute = colour offset in high nibble */
static void putcell(uint8_t x, uint8_t y, uint8_t ch, uint8_t coff)
{
    uint8_t *p = (uint8_t *)(TILEMAP_BASE + (((uint16_t)y * TM_W + x) << 1));
    p[0] = ch;
    p[1] = (uint8_t)(coff << 4);
}

static void print_str(uint8_t x, uint8_t y, const char *s, uint8_t coff)
{
    while (*s && x < TM_W) {
        putcell(x++, y, (uint8_t)*s, coff);
        s++;
    }
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
 * INPUT - blocking key read with debounce
 * ============================================================ */

static int getkey(void)
{
    int k;
    do {
        k = in_inkey();
    } while (k == 0);
    in_wait_nokey();        /* wait for release to avoid auto-repeat */
    return k;
}

/* ============================================================
 * MAP - hand-made NetHack-style level
 *   '-' '|' wall   '.' floor   '#' corridor   '+' door
 *   '>' '<' stairs   '$' gold   '%' food
 *   'd' dog          'r' rat    ' ' rock (impassable)
 * ============================================================ */

#define MAP_W 28
#define MAP_H 11
#define OX 3              /* map -> screen column offset */
#define OY 2              /* map -> screen row offset    */

static const char *const MAP[MAP_H] = {
    "                            ",
    "   ----------               ",
    "   |........|       --------",
    "   |........|       |......|",
    "   |...$....|       |..d...|",
    "   |........+#######+......|",
    "   |........|       |...>..|",
    "   ----------       |..%...|",
    "                    |......|",
    "                    --------",
    "                            ",
};

static char map_at(int x, int y)
{
    const char *row;
    int i;

    if (x < 0 || y < 0 || y >= MAP_H)
        return ' ';
    row = MAP[y];
    for (i = 0; i < x; i++) {
        if (row[i] == '\0')
            return ' ';     /* past end of string = rock */
    }
    return row[x] ? row[x] : ' ';
}

static int walkable(char c)
{
    /* only walls and rock block movement for now */
    return !(c == '|' || c == '-' || c == ' ');
}

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

/* ============================================================
 * GAME - hero state and main loop
 * ============================================================ */

static int hero_x = 6;    /* map column (inside room 1) */
static int hero_y = 3;    /* map row                    */

static void draw_map(void)
{
    int x, y;
    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            char c = map_at(x, y);
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

static void draw_frame(void)
{
    /* NetHack-style two-line status bar at the bottom (rows 22-23) */
    print_str(0, 22,
        "Player the Tourist      St:14 Dx:11 Co:14 In:10 Wi:8 Ch:10   Lawful",
        C_GREEN | C_BRIGHT);
    print_str(0, 23,
        "Dlvl:1  $:0  HP:12(12)  Pw:2(2)  AC:10  Xp:1/0  T:1",
        C_GREEN | C_BRIGHT);
}

/* message when stepping onto / bumping a tile */
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
    char dest = map_at(nx, ny);

    if (walkable(dest)) {
        hero_x = nx;
        hero_y = ny;
        describe(dest, 1);
    } else {
        describe(dest, 0);
    }
}

void main(void)
{
    int k;

    zx_border(C_BLACK);
    tm_init();
    tm_cls();

    draw_frame();
    draw_map();
    msg("Welcome to NetHack Next!  (80-column tilemap)");

    for (;;) {
        k = getkey();
        if (k >= 'A' && k <= 'Z')
            k += 32;                 /* normalise uppercase */

        switch (k) {
        /* NetHack vi-keys */
        case 'h': try_move(-1,  0); break;
        case 'l': try_move(+1,  0); break;
        case 'k': try_move( 0, -1); break;
        case 'j': try_move( 0, +1); break;
        case 'y': try_move(-1, -1); break;
        case 'u': try_move(+1, -1); break;
        case 'b': try_move(-1, +1); break;
        case 'n': try_move(+1, +1); break;
        /* cursor keys (caps+5/6/7/8 -> 8/10/11/9) */
        case  8:  try_move(-1,  0); break;
        case  9:  try_move(+1,  0); break;
        case 10:  try_move( 0, +1); break;
        case 11:  try_move( 0, -1); break;
        /* wait / search (passes the turn) */
        case '.':
        case 's': msg("You wait."); break;
        case 'q':
        case 'Q': msg("Be seeing you...");
                  draw_map();
                  for (;;) { }       /* halt here */
        default:  break;
        }
        draw_map();
    }
}
