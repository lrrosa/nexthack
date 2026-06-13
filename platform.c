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
#include <arch/zxn/esxdos.h>

#define TILEDEF_BASE  0x4000u
#define TILEMAP_BASE  0x6000u
#define ROM_FONT      0x3C00u

/* Text ink colours, indexed by colour offset 1..15 (offset 0 is reserved
 * for the graphic-tile master palette). */
/* These tables are read by the banked setup in platform_init.c (via extern),
 * so they are not static. They stay resident (read once at startup). */
const uint8_t inkcol[16] = {
    0x00, 0x02, 0x80, 0x82, 0x10, 0x12, 0x90, 0x92,  /* dim    */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF   /* bright */
};

/* 16-colour master palette (RRRGGGBB) used by the graphic tiles. */
const uint8_t master[16] = {
    0x00, 0x49, 0x92, 0xDB, 0xFF,   /* 0 black 1 dkgrey 2 grey 3 ltgrey 4 white */
    0x44, 0x88, 0xD5,               /* 5 dkbrown 6 brown 7 tan                  */
    0xE0, 0x1C, 0x08, 0x03,         /* 8 red 9 green 10 dkgreen 11 blue         */
    0x1F, 0xFC, 0xF0, 0xF9          /* 12 cyan 13 yellow 14 orange 15 skin      */
};

/* Each tile: 64 pixels (8 rows x 8 cols), values are master-palette indices.
 * Order matches the T_* numbering starting at T_ROCK. */
const uint8_t gfx[25][64] = {
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
    0,13,0,0,0,13,0,0, 0,0,13,13,13,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_AMULET (Amulet of Yendor: gold-set blue gem on a chain) */
    0,0,0,13,13,0,0,0, 0,0,0,13,13,0,0,0, 0,0,13,13,13,13,0,0, 0,13,11,12,11,11,13,0,
    0,13,11,11,11,11,13,0, 0,13,11,11,11,11,13,0, 0,0,13,11,11,13,0,0, 0,0,0,13,13,0,0,0 },
  { /* T_ACIDBLOB (slimy green blob with a wet highlight) */
    0,0,0,9,9,0,0,0, 0,0,9,4,9,9,0,0, 0,9,9,9,9,9,9,0, 9,9,9,9,9,9,9,9,
    9,9,10,9,9,10,9,9, 9,9,9,9,9,9,9,9, 0,9,9,9,9,9,9,0, 0,0,9,9,9,9,0,0 }
};

/* pack 64 palette indices into a 4bpp 8x8 tile (32 bytes) at a tile slot */
/* The one-time tilemap/font/tile/palette setup (pack_tile, load_gfx_tiles,
 * tm_init_font, tm_init_palette, tm_init) moved to the banked platform_init.c.
 * TILEDEF_BASE/ROM_FONT went with it; only TILEMAP_BASE is used below. */

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

/* ---- save-file I/O (NextZXOS/esxDOS). Returns FILE_ERR on failure. ---- */

uint8_t file_create(const char *name)
{
    return esx_f_open(name, (uint8_t)(ESX_MODE_W | ESX_MODE_OPEN_CREAT_TRUNC));
}

uint8_t file_open(const char *name)
{
    return esx_f_open(name, (uint8_t)(ESX_MODE_R | ESX_MODE_OPEN_EXIST));
}

void file_write(uint8_t h, const void *src, uint16_t n)
{
    esx_f_write(h, (void *)src, n);
}

void file_read(uint8_t h, void *dst, uint16_t n)
{
    esx_f_read(h, dst, n);
}

void file_close(uint8_t h)
{
    esx_f_close(h);
}

void file_remove(const char *name)
{
    esx_f_unlink(name);
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
