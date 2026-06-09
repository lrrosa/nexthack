/* SPDX-License-Identifier: GPL-3.0-or-later */
/* platform.h - ZX Spectrum Next hardware layer: tilemap display, tile/font
 * graphics, text/messages and keyboard. Nothing game-specific lives here. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <arch/zxn.h>     /* ZXN_WRITE_REG / ZXN_READ_REG, zx_border */
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

/* tilemap dimensions (characters) */
#define TM_W 80
#define TM_H 32

/* set up the tilemap, font, graphic tiles and palette */
void tm_init(void);

/* drawing */
void    putcell(uint8_t x, uint8_t y, uint8_t ch, uint8_t coff);   /* font tile */
void    puttile(uint8_t x, uint8_t y, uint8_t tile);               /* graphic tile */
uint8_t *tm_cell_ptr(uint8_t x, uint8_t y);   /* address of a tilemap cell */
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

#endif /* PLATFORM_H */
