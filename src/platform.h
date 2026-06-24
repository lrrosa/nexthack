/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* platform.h - hardware layer: display, tile/font graphics, text/messages and
 * keyboard. Two targets share this API: the ZX Spectrum Next (+zxn, hardware
 * tilemap, __ZXNEXT) and the plain ZX Spectrum 128K (+zx, ULA + 1-bit UDGs).
 * Nothing game-specific lives here. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#ifdef __ZXNEXT
#include <arch/zxn.h>     /* ZXN_WRITE_REG / ZXN_READ_REG, zx_border */
#else
#include <arch/zx.h>      /* ULA: zx_border, screen/attr address helpers (+zx) */
#endif
#include <input.h>        /* in_inkey, in_wait_nokey, in_pause       */

/* ---- colour offsets (0..15): low 3 bits = hue, +8 = bright ---- */
#define C_BLACK   0
#define C_BLUE    1
#define C_RED     2
#define C_MAGENTA 3
#define C_GREEN   4
#define C_CYAN    5
#define C_YELLOW  6
#define C_WHITE   7
#define C_BRIGHT  8

/* ---- graphic tile numbers (past the 128 ROM-font tiles) ---- */
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
#define T_WEAPON 141
#define T_ARMOR  142
#define T_POTION 143
#define T_KOBOLD 144
#define T_ORC    145
#define T_SNAKE  146
#define T_BAT    147
#define T_ZOMBIE 148
#define T_SCROLL 149
#define T_RING   150
#define T_AMULET 151
#define T_ACIDBLOB 152
#define T_SHOPWALL 153   /* a shop's walls: warm tan/brown bricks (vs grey T_WALL) */
#define T_KEEPER   154   /* the shopkeeper: orange robe, distinct from the blue hero */
#define T_LEPRECHAUN  155  /* small green humanoid; steals gold and flees */
#define T_YELLOWLIGHT 156  /* glowing orb; blinds on contact              */
#define T_TRAP        157  /* a sprung trap (revealed after you step on it) */
#define T_HOMUNCULUS  158  /* small imp; its bite puts you to sleep         */
#define T_WRAITH      159  /* pale wraith; drains your life force           */
#define T_ALTAR       160  /* a stone altar; step on it to reveal item BUC  */

#define NTILES   33      /* T_ROCK..T_ALTAR: the graphic tiles */

/* display dimensions (characters) */
#ifdef __ZXNEXT
#define TM_W 80          /* Next hardware tilemap 80x32 */
#define TM_H 32
#else
#define TM_W 32          /* ULA 256x192 = 32x24 */
#define TM_H 24
#endif

/* one-time display setup (banked; runs once from main) */
void tm_init(void) __banked;

/* drawing */
void    putcell(uint8_t x, uint8_t y, uint8_t ch, uint8_t coff);   /* ROM-font glyph */
void    puttile(uint8_t x, uint8_t y, uint8_t tile);               /* graphic/UDG tile */
#ifdef __ZXNEXT
uint8_t *tm_cell_ptr(uint8_t x, uint8_t y);   /* address of a tilemap cell (Next) */
#else
void    puttile_attr(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr); /* UDG + explicit attr */
/* 1-bit map-tile shapes + their base inks, built from udg_src[] at startup.
 * udg_bitmap lives in Bank 5 (0x6680, after the status shadow), which the CPU
 * always sees at 0x4000-0x7FFF on the 128K -- so the resident blits read it in
 * place while it costs 0 resident BSS (232 B reclaimed). Keep 0x6680 clear of
 * VIEW_SHADOW (0x6000), SSHADOW (0x6600) and the BFS scratch (0x7400). SDCC
 * rejects a cast to a pointer-to-array, so it is a flat pointer indexed
 * udg_bitmap[tile*8 + row] (the asm blits use the same base + index*8). */
#define udg_bitmap ((uint8_t *)0x6680u)
extern const uint8_t udg_ink[NTILES];
#endif
uint8_t print_str(uint8_t x, uint8_t y, const char *s, uint8_t coff);
uint8_t put_uint(uint8_t x, uint8_t y, uint16_t v, uint8_t coff);
void    clear_line(uint8_t y, uint8_t coff);
void    tm_cls(void);

/* message line (row 0) helpers */
void msg(const char *s);
void msg2(const char *a, const char *b, const char *c);
void msg_num(const char *a, uint16_t n, const char *c);

/* keyboard: returns the key currently held (does not wait for release) */
int getkey(void);

/* ---- save-file I/O via NextZXOS/esxDOS ---- */
#define FILE_ERR 0xFF                        /* handle returned on failure */
uint8_t file_create(const char *name);       /* create/truncate for writing */
uint8_t file_open(const char *name);         /* open an existing file to read */
void    file_write(uint8_t h, const void *src, uint16_t n);
void    file_read(uint8_t h, void *dst, uint16_t n);
void    file_close(uint8_t h);
void    file_remove(const char *name);

#endif /* PLATFORM_H */
