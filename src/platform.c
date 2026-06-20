/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* platform.c - hardware layer for two targets:
 *
 *  __ZXNEXT (+zxn): the Next hardware TILEMAP (80x32). Bank 5 (0x4000-0x7FFF):
 *    0x4000 tile definitions (4bpp 8x8), 0x6000 tilemap (id+attr per cell).
 *    Font tiles 0..127 from the ROM font, coloured by the attribute's palette
 *    offset; graphic tiles 128+ use the 16-colour master palette.
 *
 *  +zx (plain 128K): the ULA screen (256x192 = 32x24). 0x4000 pixel bitmap,
 *    0x5800 attributes. Text copies the ROM 1bpp font straight into the bitmap;
 *    map cells are 27 1-bit UDGs (udg_bitmap[]) with one ink each.
 *
 * The game logic and the platform API are identical for both. */

#include "platform.h"
#include <arch/zxn/esxdos.h>

#ifdef __ZXNEXT
/* ---------------- ZX Spectrum Next (hardware tilemap) ---------------- */
#define TILEMAP_BASE  0x6000u

/* Text ink colours (offset 1..15) + the 16-colour master palette, read once at
 * startup by platform_init's palette setup (via extern); resident. */
const uint8_t inkcol[16] = {
    0x00, 0x02, 0x80, 0x82, 0x10, 0x12, 0x90, 0x92,  /* dim    */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF   /* bright */
};
const uint8_t master[16] = {
    0x00, 0x49, 0x92, 0xDB, 0xFF,   /* 0 black 1 dkgrey 2 grey 3 ltgrey 4 white */
    0x44, 0x88, 0xD5,               /* 5 dkbrown 6 brown 7 tan                  */
    0xE0, 0x1C, 0x08, 0x03,         /* 8 red 9 green 10 dkgreen 11 blue         */
    0x1F, 0xFC, 0xF0, 0xF9          /* 12 cyan 13 yellow 14 orange 15 skin      */
};

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

void tm_cls(void)
{
    uint8_t y;
    for (y = 0; y < TM_H; y++)
        clear_line(y, C_BLACK);
}

#else
/* -------------------- plain ZX Spectrum 128K (ULA) -------------------- */
#define SCRN_BASE 0x4000u      /* pixel bitmap */
#define ATTR_BASE 0x5800u      /* attributes   */
#define ROM_FONT  0x3C00u      /* ROM 8x8 font (char c -> 0x3C00 + (c<<3)) */

/* 1-bit map-tile shapes (built from udg_src[] by platform_init's build_udgs)
 * + each tile's base ink (0..7); draw_map ORs in BRIGHT for in-sight cells. */
uint8_t udg_bitmap[NTILES][8];

const uint8_t udg_ink[NTILES] = {
    /* ROCK   FLOOR  WALL   CORR   DOOR   SUP    SDOWN */
       0,     7,     7,     7,     6,     7,     7,
    /* HERO   DOG    RAT    GOLD   FOOD   DOLLAR WEAPON ARMOR  POTION */
       5,     6,     7,     6,     2,     4,     7,     7,     5,
    /* KOBOLD ORC    SNAKE  BAT    ZOMBIE SCROLL RING   AMULET ACIDBLOB */
       6,     4,     4,     1,     4,     7,     6,     6,     4,  /* bat: blue */
    /* SHOPWALL KEEPER */
       6,       3        /* keeper magenta: distinct from cyan hero + yellow items */
};

/* Top pixel-line address of char cell (x,y): 0x4000 + third*0x800
 * + (row-in-third)*0x20 + x. Successive pixel lines are +0x100 apart. */
static uint8_t *cell_addr(uint8_t x, uint8_t y)
{
    return (uint8_t *)(SCRN_BASE
        + ((uint16_t)(y & 0x18) << 8)    /* third  (y>>3)*0x800 */
        + ((uint16_t)(y & 0x07) << 5)    /* row    (y&7)*0x20   */
        + x);
}

static uint8_t attr_from(uint8_t coff)
{
    /* low 3 bits = ink; bit 3 (C_BRIGHT) -> ULA bright (0x40); paper black */
    return (uint8_t)((coff & 7) | ((coff & 8) ? 0x40 : 0));
}

void putcell(uint8_t x, uint8_t y, uint8_t ch, uint8_t coff)
{
    const uint8_t *src = (const uint8_t *)(ROM_FONT + ((uint16_t)ch << 3));
    uint8_t *d = cell_addr(x, y);
    uint8_t L;
    for (L = 0; L < 8; L++) { *d = src[L]; d += 0x100; }
    *((uint8_t *)(ATTR_BASE + ((uint16_t)y << 5) + x)) = attr_from(coff);
}

void puttile(uint8_t x, uint8_t y, uint8_t tile)
{
    const uint8_t *src = udg_bitmap[tile - T_ROCK];
    uint8_t *d = cell_addr(x, y);
    uint8_t L;
    for (L = 0; L < 8; L++) { *d = src[L]; d += 0x100; }
    /* drawn "in hand": full BRIGHT ink (status bar / inventory use this) */
    *((uint8_t *)(ATTR_BASE + ((uint16_t)y << 5) + x)) =
        (uint8_t)(udg_ink[tile - T_ROCK] | 0x40);
}

/* Draw a UDG tile with an explicit attribute (the map renderer's fog-of-war
 * dim: BRIGHT in sight, plain when only remembered). */
void puttile_attr(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr)
{
    const uint8_t *src = udg_bitmap[tile - T_ROCK];
    uint8_t *d = cell_addr(x, y);
    uint8_t L;
    for (L = 0; L < 8; L++) { *d = src[L]; d += 0x100; }
    *((uint8_t *)(ATTR_BASE + ((uint16_t)y << 5) + x)) = attr;
}

void tm_cls(void)
{
    uint8_t *p = (uint8_t *)SCRN_BASE;
    uint16_t i;
    for (i = 0; i < 6144; i++) p[i] = 0;          /* blank the bitmap   */
    p = (uint8_t *)ATTR_BASE;
    for (i = 0; i < 768; i++) p[i] = 0;            /* black ink on black */
}
#endif  /* __ZXNEXT */

/* ---- text helpers (shared) ---- */

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

/* ---- save-file I/O (NextZXOS/esxDOS). Returns FILE_ERR on failure. ---- */

/* esxDOS lives on RST 8. The Next always has NextZXOS, but a plain 128K usually
 * has no DivMMC -- there RST 8 is the ROM error restart, so an unguarded esxDOS
 * call drops to BASIC. On +zx we probe once (esxdetect.asm) and, if absent,
 * make every file op fail cleanly (the game then just runs without save/load).*/
uint8_t esxdos_ok = 1;          /* assumed present; +zx confirms below */

#ifndef __ZXNEXT
extern void esxdos_detect(void);    /* esxdetect.asm: sets esxdos_ok 1/0 */
static uint8_t esx_checked = 0;
static uint8_t esx_ready(void)
{
    if (!esx_checked) { esxdos_detect(); esx_checked = 1; }
    return esxdos_ok;
}
#endif

uint8_t file_create(const char *name)
{
#ifndef __ZXNEXT
    if (!esx_ready()) return FILE_ERR;
#endif
    return esx_f_open(name, (uint8_t)(ESX_MODE_W | ESX_MODE_OPEN_CREAT_TRUNC));
}

uint8_t file_open(const char *name)
{
#ifndef __ZXNEXT
    if (!esx_ready()) return FILE_ERR;
#endif
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

/* ---- message line (row 0). Overwrites left-to-right and pads with spaces
 * (no full-line clear first), so it does not flicker. ---- */

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
