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

extern const uint8_t gfx[25][64];   /* graphic-tile pixels (resident, platform.c) */
extern const uint8_t master[16];    /* graphic-tile master palette                */
extern const uint8_t inkcol[16];    /* text ink colours                           */

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
