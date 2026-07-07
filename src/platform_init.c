/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* platform_init.c - BANKED one-time display setup (runs once from main).
 * __ZXNEXT: tilemap/font/4bpp-tile/palette setup + the const-banked gfx[]
 * table. +zx: build the 27 1-bit UDGs (from udg_src[]) + clear the ULA. */

#include "platform.h"

#ifdef __ZXNEXT
#include <arch/zxn.h>

#pragma codeseg PAGE_22_CODE

#define TILEDEF_BASE  0x4000u
#define ROM_FONT      0x3C00u

extern const uint8_t master[16];    /* graphic-tile master palette  (platform.c) */
extern const uint8_t inkcol[16];    /* text ink colours             (platform.c) */

/* The 1728-byte graphic-tile table is read ONCE, here, by load_gfx_tiles(), so
 * it lives in this banked module's const section (PAGE_22_CODE) instead of
 * resident rodata -- reclaiming ~1.7 KB of the tight resident budget. It is
 * reachable because load_gfx_tiles() runs with PAGE_22 mapped in.
 * Each tile: 64 pixels (8 rows x 8 cols), values are master-palette indices.
 * Order matches the T_* numbering starting at T_ROCK. */
#pragma constseg PAGE_22_CODE
const uint8_t gfx[NTILES][64] = {
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
    9,9,10,9,9,10,9,9, 9,9,9,9,9,9,9,9, 0,9,9,9,9,9,9,0, 0,0,9,9,9,9,0,0 },
  { /* T_SHOPWALL (T_WALL brick recoloured warm: 7 tan / 6 brown / 5 dkbrown) */
    7,7,7,7,7,7,7,5, 7,6,6,6,6,6,6,5, 7,6,6,6,6,6,6,5, 5,5,5,5,5,5,5,5,
    7,7,7,5,7,7,7,7, 6,6,6,5,6,6,6,6, 6,6,6,5,6,6,6,6, 5,5,5,5,5,5,5,5 },
  { /* T_KEEPER (shopkeeper: T_HERO recoloured with an orange 14 robe) */
    0,0,0,15,15,0,0,0, 0,0,0,15,15,0,0,0, 0,0,15,15,15,15,0,0, 0,0,0,14,14,0,0,0,
    0,14,14,14,14,14,14,0, 0,0,14,14,14,14,0,0, 0,0,14,0,0,14,0,0, 0,0,14,0,0,14,0,0 },
  { /* T_LEPRECHAUN (small green sprite with a tan hat) */
    0,0,0,7,7,0,0,0, 0,0,7,7,7,7,0,0, 0,0,0,9,9,0,0,0, 0,0,9,9,9,9,0,0,
    0,9,9,9,9,9,9,0, 0,0,9,9,9,9,0,0, 0,0,9,0,0,9,0,0, 0,0,9,0,0,9,0,0 },
  { /* T_YELLOWLIGHT (glowing yellow orb with an orange core) */
    0,0,0,13,13,0,0,0, 0,0,13,13,13,13,0,0, 0,13,13,14,14,13,13,0, 0,13,14,14,14,14,13,0,
    0,13,14,14,14,14,13,0, 0,13,13,14,14,13,13,0, 0,0,13,13,13,13,0,0, 0,0,0,13,13,0,0,0 },
  { /* T_TRAP (a red ^ chevron -- a sprung trap) */
    0,0,0,0,0,0,0,0, 0,0,0,8,8,0,0,0, 0,0,8,8,8,8,0,0, 0,8,8,0,0,8,8,0,
    8,8,0,0,0,0,8,8, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 },
  { /* T_HOMUNCULUS (small red imp with horns and yellow eyes) */
    0,8,0,0,0,0,8,0, 0,0,8,8,8,8,0,0, 0,8,13,8,8,13,8,0, 0,0,8,8,8,8,0,0,
    0,0,0,8,8,0,0,0, 0,0,8,8,8,8,0,0, 0,0,8,0,0,8,0,0, 0,0,8,0,0,8,0,0 },
  { /* T_WRAITH (pale grey hooded wraith, dark eyes, wispy below) */
    0,0,3,3,3,3,0,0, 0,3,4,4,4,4,3,0, 0,3,4,8,8,4,3,0, 0,3,4,4,4,4,3,0,
    0,0,3,4,4,3,0,0, 0,0,3,3,3,3,0,0, 0,3,0,3,0,3,0,0, 0,0,3,0,3,0,3,0 },
  { /* T_ALTAR (a pale stone slab resting on a grey pedestal) */
    0,0,0,0,0,0,0,0, 0,4,4,4,4,4,4,0, 0,3,3,3,3,3,3,0, 0,0,2,2,2,2,0,0,
    0,0,2,2,2,2,0,0, 0,0,2,2,2,2,0,0, 0,3,3,3,3,3,3,0, 0,0,0,0,0,0,0,0 },
  { /* T_WAND (brown grip, tan shaft, glowing cyan tip) */
    0,0,0,0,0,12,12,0, 0,0,0,0,0,12,7,0, 0,0,0,0,7,7,0,0, 0,0,0,7,7,0,0,0,
    0,0,7,7,0,0,0,0, 0,7,7,0,0,0,0,0, 6,6,0,0,0,0,0,0, 6,0,0,0,0,0,0,0 },
  { /* T_FEYE (floating eye: blue orb, white sclera, cyan iris, blue pupil) */
    0,0,11,11,11,11,0,0, 0,11,4,4,4,4,11,0, 11,4,4,12,12,4,4,11,
    11,4,12,11,11,12,4,11, 11,4,12,11,11,12,4,11, 11,4,4,12,12,4,4,11,
    0,11,4,4,4,4,11,0, 0,0,11,11,11,11,0,0 },
  { /* T_BOOK (spellbook: red covers, white page edges, gold clasp) */
    0,0,0,0,0,0,0,0, 0,8,8,8,8,8,8,0, 8,4,4,4,4,4,8,13,
    8,4,4,4,4,4,8,13, 8,4,4,4,4,4,8,13, 8,4,4,4,4,4,8,13,
    0,8,8,8,8,8,8,0, 0,0,0,0,0,0,0,0 },
  { /* T_FOUNTAIN (a grey basin on a stem, brimming with cyan water) */
    0,0,0,0,0,0,0,0, 0,2,2,2,2,2,2,0, 0,2,12,12,12,12,2,0,
    0,2,12,12,12,12,2,0, 0,0,2,12,12,2,0,0, 0,0,0,2,2,0,0,0,
    0,0,0,2,2,0,0,0, 0,0,2,2,2,2,0,0 },
  { /* T_TROLL (hulking dark-green brute: red eyes, white tusks, huge arms) */
    0,0,10,10,10,10,0,0, 0,10,8,10,10,8,10,0, 0,4,10,10,10,10,4,0, 0,0,10,10,10,10,0,0,
    10,10,10,10,10,10,10,10, 10,0,10,10,10,10,0,10, 0,0,10,0,0,10,0,0, 0,10,10,0,0,10,10,0 },
  { /* T_VAMPIRE (pale face, red eyes, white fangs, dark spread cape) */
    0,0,1,15,15,1,0,0, 0,0,8,15,15,8,0,0, 0,0,15,4,4,15,0,0, 0,1,15,15,15,15,1,0,
    1,1,8,1,1,8,1,1, 0,1,1,1,1,1,1,0, 0,1,1,0,0,1,1,0, 0,1,0,0,0,0,1,0 },
  { /* T_DRAGON (red dragon on the wing: yellow eyes, orange membranes) */
    0,0,0,8,8,0,0,0, 0,0,8,13,13,8,0,0, 8,0,8,8,8,8,0,8, 8,8,8,8,8,8,8,8,
    8,14,8,8,8,8,14,8, 0,0,8,8,8,8,0,0, 0,0,0,8,8,0,0,0, 0,0,0,0,8,0,0,0 },
  { /* T_PRIEST (the Amulet's keeper: gold mitre, red eyes, sashed red robe) */
    0,0,0,13,13,0,0,0, 0,0,13,13,13,13,0,0, 0,0,15,8,8,15,0,0, 0,0,15,15,15,15,0,0,
    0,8,8,8,8,8,8,0, 8,8,8,13,13,8,8,8, 0,8,8,8,8,8,8,0, 0,8,8,0,0,8,8,0 },
  { /* T_MIMIC (a revealed mimic: brown chest, yellow eyes, toothy maw) */
    0,6,6,6,6,6,6,0, 6,6,13,6,6,13,6,6, 6,6,6,6,6,6,6,6, 4,6,4,6,4,6,4,6,
    6,4,6,4,6,4,6,4, 6,6,6,6,6,6,6,6, 0,6,6,6,6,6,6,0, 0,0,0,0,0,0,0,0 }
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
    for (i = 0; i < NTILES; i++)
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

#else  /* plain ZX Spectrum 128K */

#pragma codeseg BANK_3

/* 27 monochrome map tiles (1 byte/row, MSB = leftmost pixel), order matches
 * the T_ROCK.. numbering. Drawn for clarity at 8x8 on a single-ink cell. */
#pragma constseg BANK_3
static const uint8_t udg_src[NTILES][8] = {
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, /* ROCK     (blank)        */
    { 0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00 }, /* FLOOR    (centre dot)   */
    { 0xFF,0x88,0x88,0xFF,0x22,0x22,0xFF,0x88 }, /* WALL     (brick)        */
    { 0x00,0x20,0x00,0x04,0x00,0x40,0x00,0x08 }, /* CORR     (speckle)      */
    { 0x7E,0x42,0x42,0x4A,0x42,0x42,0x42,0x7E }, /* DOOR     (door + knob)  */
    { 0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF }, /* SUP      (ascend ramp)  */
    { 0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01 }, /* SDOWN    (descend ramp) */
    { 0x18,0x18,0x3C,0x5A,0x18,0x18,0x24,0x42 }, /* HERO     (person)       */
    { 0x00,0x60,0xF0,0x7F,0x7F,0x24,0x24,0x00 }, /* DOG      (quadruped)    */
    { 0x00,0x00,0x01,0x0E,0x1F,0x1F,0x0A,0x00 }, /* RAT      (rodent)       */
    { 0x00,0x3C,0x7E,0x7E,0x7E,0x7E,0x3C,0x00 }, /* GOLD     (coin)         */
    { 0x00,0x06,0x0F,0x1E,0x3C,0x78,0x30,0x00 }, /* FOOD     (drumstick)    */
    { 0x10,0x7C,0xD6,0x70,0x1C,0xD6,0x7C,0x10 }, /* DOLLAR   ($ glyph)      */
    { 0x18,0x18,0x18,0x18,0x18,0x7E,0x18,0x18 }, /* WEAPON   (dagger)       */
    { 0x7E,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00 }, /* ARMOR    (shield)       */
    { 0x18,0x18,0x3C,0x7E,0x7E,0x7E,0x7E,0x3C }, /* POTION   (flask)        */
    { 0x18,0x18,0x3C,0x18,0x3C,0x24,0x24,0x00 }, /* KOBOLD   (small humanoid)*/
    { 0x3C,0x7E,0x5A,0x7E,0x3C,0x3C,0x66,0x66 }, /* ORC      (brute)        */
    { 0x0C,0x12,0x0C,0x18,0x30,0x48,0x30,0x00 }, /* SNAKE    (serpent)      */
    { 0x00,0xC3,0xE7,0xFF,0xDB,0x99,0x00,0x00 }, /* BAT  spread wings+head   */
    { 0x18,0x18,0x3C,0x3C,0x18,0x3C,0x24,0x24 }, /* ZOMBIE   (shambler)     */
    { 0x7E,0x81,0xBD,0x81,0xBD,0x81,0xBD,0x7E }, /* SCROLL   (parchment)    */
    { 0x00,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00 }, /* RING     (hollow ring)  */
    { 0x18,0x24,0x42,0x3C,0x18,0x3C,0x24,0x00 }, /* AMULET   (pendant)      */
    { 0x00,0x3C,0x7E,0xFF,0xFF,0xFF,0x7E,0x3C }, /* ACIDBLOB (blob)         */
    { 0xFF,0xA8,0xA8,0xFF,0x2A,0x2A,0xFF,0xA8 }, /* SHOPWALL (denser brick) */
    { 0x18,0x3C,0x18,0x5A,0x18,0x18,0x24,0x42 }, /* KEEPER   (person variant)*/
    { 0x18,0x3C,0x18,0x3C,0x7E,0x18,0x24,0x42 }, /* LEPRECHAUN (sprite + hat)*/
    { 0x00,0x18,0x3C,0x7E,0x7E,0x3C,0x18,0x00 }, /* YELLOWLIGHT (glowing orb)*/
    { 0x00,0x18,0x3C,0x66,0xC3,0x81,0x00,0x00 }, /* TRAP  (^ chevron)        */
    { 0x42,0x3C,0x5A,0x3C,0x18,0x3C,0x66,0x00 }, /* HOMUNCULUS (horned imp)  */
    { 0x3C,0x7E,0x66,0x7E,0x3C,0x3C,0x5A,0x24 }, /* WRAITH (hooded, wispy)   */
    { 0x00,0x7E,0x7E,0x3C,0x3C,0x3C,0x7E,0x00 }, /* ALTAR (slab on pedestal) */
    { 0x06,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80 }, /* WAND  (diagonal stick)   */
    { 0x3C,0x42,0x99,0xBD,0xBD,0x99,0x42,0x3C }, /* FEYE  (lidless eye)      */
    { 0x00,0x7C,0x7E,0x46,0x46,0x7E,0x7C,0x00 }, /* BOOK  (closed covers)    */
    { 0x00,0x7E,0x42,0x7E,0x3C,0x18,0x18,0x7E }, /* FOUNTAIN (basin on a stem)*/
    { 0x3C,0x5A,0xFF,0xDB,0x3C,0x3C,0x24,0x66 }, /* TROLL   (hulking brute)  */
    { 0x3C,0x5A,0x18,0x7E,0xFF,0xDB,0x99,0x81 }, /* VAMPIRE (spread cape)    */
    { 0x18,0x99,0xDB,0xFF,0x7E,0x3C,0x18,0x08 }, /* DRAGON  (on the wing)    */
    { 0x18,0x3C,0x24,0x3C,0x7E,0xFF,0xFF,0x66 }, /* PRIEST  (mitred, robed)  */
    { 0x7E,0x5A,0xFF,0xAA,0x55,0xFF,0x7E,0x00 }  /* MIMIC   (toothy chest)   */
};

/* Copy the hand-drawn tiles into udg_bitmap[] (Bank 5, see platform.h) that the
 * resident blits read; this bank's const udg_src is reachable here in place.
 * udg_bitmap is a flat pointer, so index it tile*8 + row. */
static void build_udgs(void)
{
    uint8_t t, row;
    for (t = 0; t < NTILES; t++)
        for (row = 0; row < 8; row++)
            udg_bitmap[(uint16_t)t * 8 + row] = udg_src[t][row];
}

void tm_init(void) __banked
{
    build_udgs();   /* reads udg_src[] (this bank), fills resident udg_bitmap[] */
    tm_cls();       /* blank the ULA screen (resident leaf) */
}

#endif  /* __ZXNEXT */
