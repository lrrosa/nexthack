/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* platform_init.c - BANKED one-time tilemap/font/tile/palette setup, split out
 * of platform.c so this startup-only code lives in PAGE_22_CODE. It runs once
 * (tm_init, from main) and then never again, so the trampoline cost is nil.
 *
 * The palette/tile source tables (gfx/master/inkcol) stay RESIDENT in
 * platform.c and are read here via extern -- banked code reads resident data
 * directly. (Banking the 1600-byte gfx[] too would need const-section banking;
 * left resident for now.) */

#include "platform.h"
#include <arch/zxn.h>

#pragma codeseg PAGE_22_CODE

#define TILEDEF_BASE  0x4000u
#define ROM_FONT      0x3C00u

extern const uint8_t master[16];    /* graphic-tile master palette  (platform.c) */
extern const uint8_t inkcol[16];    /* text ink colours             (platform.c) */

/* The 1600-byte graphic-tile table is read ONCE, here, by load_gfx_tiles(), so
 * it lives in this banked module's const section (PAGE_22_CODE) instead of
 * resident rodata -- reclaiming ~1.6 KB of the tight resident budget. It is
 * reachable because load_gfx_tiles() runs with PAGE_22 mapped in.
 * Each tile: 64 pixels (8 rows x 8 cols), values are master-palette indices.
 * Order matches the T_* numbering starting at T_ROCK. */
#pragma constseg PAGE_22_CODE
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
    for (i = 0; i < 25; i++)
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

void tm_init(void) __banked
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
