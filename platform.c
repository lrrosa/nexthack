/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* platform.c - ZX Spectrum Next hardware layer.
 *
 * Renders on the hardware TILEMAP (80x32). Memory map (Bank 5, fixed at
 * 0x4000-0x7FFF, free since the program lives at 0x8000+):
 *   0x4000  tile definitions (4bpp 8x8 tiles, 32 bytes each)
 *   0x6000  tilemap (80x32 entries x 2 bytes: tile id + attribute)
 *
 * Font tiles 0..127 come from the ROM font at 0x3C00 and are coloured per
 * cell via the attribute's palette offset (offsets 1..15). Graphic tiles
 * 128+ are full colour and use palette offset 0 (the 16-colour master
 * palette). The ULA layer is disabled so only the tilemap is shown.
 */

#include "platform.h"

#define TILEDEF_BASE  0x4000u
#define TILEMAP_BASE  0x6000u
#define ROM_FONT      0x3C00u

/* Text ink colours, indexed by colour offset 1..15 (offset 0 is reserved
 * for the graphic-tile master palette). */
static const uint8_t inkcol[16] = {
    0x00, 0x02, 0x80, 0x82, 0x10, 0x12, 0x90, 0x92,  /* dim    */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF   /* bright */
};

/* 16-colour master palette (RRRGGGBB) used by the graphic tiles. */
static const uint8_t master[16] = {
    0x00, 0x49, 0x92, 0xDB, 0xFF,   /* 0 black 1 dkgrey 2 grey 3 ltgrey 4 white */
    0x44, 0x88, 0xD5,               /* 5 dkbrown 6 brown 7 tan                  */
    0xE0, 0x1C, 0x08, 0x03,         /* 8 red 9 green 10 dkgreen 11 blue         */
    0x1F, 0xFC, 0xF0, 0xF9          /* 12 cyan 13 yellow 14 orange 15 skin      */
};

/* Each tile: 64 pixels (8 rows x 8 cols), values are master-palette indices.
 * Order matches the T_* numbering starting at T_ROCK. */
static const uint8_t gfx[23][64] = {
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
  { /* T_DOOR (arched wooden door: grey frame, brown planks, knob) */
    0,0,3,3,3,3,0,0, 0,3,5,5,5,5,3,0, 3,5,6,6,6,6,5,3, 3,5,6,6,6,6,5,3,
    3,5,6,6,13,6,5,3, 3,5,6,6,6,6,5,3, 3,5,6,6,6,6,5,3, 0,3,3,3,3,3,3,0 },
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
    0,0,0,9,0,9,0,0, 0,9,9,9,9,9,0,0, 0,0,0,9,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_WEAPON (dagger: white blade, brown hilt) */
    0,0,0,0,0,0,4,0, 0,0,0,0,0,4,4,0, 0,0,0,0,4,4,0,0, 0,0,0,4,4,0,0,0,
    0,0,6,4,4,0,0,0, 0,6,6,6,0,0,0,0, 6,6,0,6,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_ARMOR (shield: grey rim, light interior) */
    0,2,2,2,2,2,2,0, 0,2,3,3,3,3,2,0, 0,2,3,2,2,3,2,0, 0,2,3,3,3,3,2,0,
    0,0,2,3,3,2,0,0, 0,0,2,3,3,2,0,0, 0,0,0,2,2,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_POTION (flask with blue liquid) */
    0,0,0,3,3,0,0,0, 0,0,0,3,3,0,0,0, 0,0,3,3,3,3,0,0, 0,0,3,11,11,3,0,0,
    0,3,11,11,11,11,3,0, 0,3,11,11,11,11,3,0, 0,3,11,11,11,11,3,0, 0,0,3,3,3,3,0,0 },
  { /* T_KOBOLD (small orange/brown humanoid) */
    0,0,0,14,14,0,0,0, 0,0,0,14,14,0,0,0, 0,0,14,14,14,14,0,0, 0,0,0,5,5,0,0,0,
    0,5,5,5,5,5,5,0, 0,0,5,5,5,5,0,0, 0,0,5,0,0,5,0,0, 0,0,5,0,0,5,0,0 },
  { /* T_ORC (green brute) */
    0,0,9,9,9,9,0,0, 0,9,10,9,9,10,9,0, 0,9,9,9,9,9,9,0, 0,0,9,8,8,9,0,0,
    9,9,9,9,9,9,9,9, 0,9,9,9,9,9,9,0, 0,9,0,9,9,0,9,0, 0,9,0,0,0,0,9,0 },
  { /* T_SNAKE (coiled green serpent) */
    0,0,0,0,0,0,0,0, 0,0,9,9,0,0,0,0, 0,9,10,10,9,0,0,0, 0,9,9,9,0,0,0,0,
    0,0,0,9,9,0,0,0, 0,0,0,0,9,9,0,0, 0,0,9,9,9,9,0,0, 0,9,9,0,0,9,0,0 },
  { /* T_BAT (grey, wings spread) */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 2,0,2,2,2,2,0,2, 2,2,1,2,2,1,2,2,
    0,2,2,2,2,2,2,0, 0,0,2,0,0,2,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_ZOMBIE (rotting dark-green humanoid) */
    0,0,0,10,10,0,0,0, 0,0,0,10,9,0,0,0, 0,0,10,10,10,10,0,0, 0,0,0,10,10,0,0,0,
    0,10,10,10,10,10,0,0, 0,0,10,10,10,0,0,0, 0,0,10,0,10,0,0,0, 0,0,10,0,10,0,0,0 },
  { /* T_SCROLL (parchment with tan rolled ends) */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 7,4,4,4,4,4,4,7, 7,4,3,3,3,3,4,7,
    7,4,4,4,4,4,4,7, 7,4,3,3,3,3,4,7, 7,4,4,4,4,4,4,7, 0,0,0,0,0,0,0,0 },
  { /* T_RING (gold ring with a gem) */
    0,0,0,12,0,0,0,0, 0,0,13,13,13,0,0,0, 0,13,0,0,0,13,0,0, 0,13,0,0,0,13,0,0,
    0,13,0,0,0,13,0,0, 0,0,13,13,13,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 }
};

/* pack 64 palette indices into a 4bpp 8x8 tile (32 bytes) at a tile slot */
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
    for (i = 0; i < 23; i++)
        pack_tile((uint8_t)(T_ROCK + i), gfx[i]);
}

/* expand the 1bpp ROM font into 4bpp font tiles 0..127 */
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

    /* offset 0 (indices 0..15): master palette for graphic tiles, full bright */
    ZXN_WRITE_REG(0x40, 0x00);
    for (i = 0; i < 16; i++)
        ZXN_WRITE_REG(0x41, master[i]);

    /* offset 1 (indices 16..31): the master palette with every RGB channel
     * halved, for remembered, out-of-sight terrain. Half is the darkest a grey
     * can go and stay neutral on the Next's 3-3-2 colour (dimmer tints it). */
    ZXN_WRITE_REG(0x40, 16);
    for (i = 0; i < 16; i++) {
        uint8_t c = master[i];
        uint8_t r = (uint8_t)((c >> 5) & 7);
        uint8_t g = (uint8_t)((c >> 2) & 7);
        uint8_t b = (uint8_t)(c & 3);
        ZXN_WRITE_REG(0x41,
            (uint8_t)(((r >> 1) << 5) | ((g >> 1) << 2) | (b >> 1)));
    }

    /* offsets 2..15: black paper + ink colour, for coloured text */
    for (o = 2; o < 16; o++) {
        ZXN_WRITE_REG(0x40, (uint8_t)(o << 4));   /* index o*16        */
        ZXN_WRITE_REG(0x41, 0x00);                /* o*16+0 : black    */
        ZXN_WRITE_REG(0x41, inkcol[o]);           /* o*16+1 : ink      */
    }
}

void tm_init(void)
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

/* ---- drawing ---- */

void putcell(uint8_t x, uint8_t y, uint8_t ch, uint8_t coff)
{
    uint8_t *p = (uint8_t *)(TILEMAP_BASE + (((uint16_t)y * TM_W + x) << 1));
    p[0] = ch;
    p[1] = (uint8_t)(coff << 4);
}

void puttile(uint8_t x, uint8_t y, uint8_t tile)
{
    uint8_t *p = (uint8_t *)(TILEMAP_BASE + (((uint16_t)y * TM_W + x) << 1));
    p[0] = tile;
    p[1] = 0;     /* palette offset 0 -> master palette */
}

uint8_t *tm_cell_ptr(uint8_t x, uint8_t y)
{
    return (uint8_t *)(TILEMAP_BASE + (((uint16_t)y * TM_W + x) << 1));
}

uint8_t print_str(uint8_t x, uint8_t y, const char *s, uint8_t coff)
{
    while (*s && x < TM_W) {
        putcell(x++, y, (uint8_t)*s, coff);
        s++;
    }
    return x;
}

uint8_t put_uint(uint8_t x, uint8_t y, uint16_t v, uint8_t coff)
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

void clear_line(uint8_t y, uint8_t coff)
{
    uint8_t x;
    for (x = 0; x < TM_W; x++)
        putcell(x, y, ' ', coff);
}

void tm_cls(void)
{
    uint8_t y;
    for (y = 0; y < TM_H; y++)
        clear_line(y, C_BLACK);
}

/* ---- message line (row 0) ---- */

/* Messages overwrite the line left-to-right and pad the rest with spaces
 * (no full-line clear first), so the message line does not flicker. */
static void pad_eol(uint8_t x, uint8_t y)
{
    while (x < TM_W)
        putcell(x++, y, ' ', C_WHITE);
}

void msg(const char *s)
{
    pad_eol(print_str(0, 0, s, C_WHITE | C_BRIGHT), 0);
}

void msg2(const char *a, const char *b, const char *c)
{
    uint8_t x;
    x = print_str(0, 0, a, C_WHITE | C_BRIGHT);
    x = print_str(x, 0, b, C_WHITE | C_BRIGHT);
    x = print_str(x, 0, c, C_WHITE | C_BRIGHT);
    pad_eol(x, 0);
}

void msg_num(const char *a, uint16_t n, const char *c)
{
    uint8_t x;
    x = print_str(0, 0, a, C_WHITE | C_BRIGHT);
    x = put_uint(x, 0, n, C_WHITE | C_BRIGHT);
    x = print_str(x, 0, c, C_WHITE | C_BRIGHT);
    pad_eol(x, 0);
}

/* ---- input ---- */

int getkey(void)
{
    int k;
    do {
        k = in_inkey();
    } while (k == 0);
    return k;
}
