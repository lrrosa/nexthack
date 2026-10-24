/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* platform.c - hardware layer for two targets:
 *
 *  __ZXNEXT (+zxn): the Next hardware TILEMAP (80x32). Bank 5 (0x4000-0x7FFF):
 *    0x4000 tile definitions (4bpp 8x8), 0x6000 tilemap (id+attr per cell).
 *    Font tiles 0..127 from the game's font, coloured by the attribute's palette
 *    offset; graphic tiles 128+ use the 16-colour master palette.
 *
 *  +zx (plain 128K): the ULA screen (256x192 = 32x24). 0x4000 pixel bitmap,
 *    0x5800 attributes. Text copies the ROM 1bpp font straight into the bitmap;
 *    map cells are 27 1-bit UDGs (udg_bitmap[]) with one ink each.
 *
 * The game logic and the platform API are identical for both. */

#include "platform.h"
#include <arch/zxn/esxdos.h>

#ifndef __ZXNEXT
/* Row 0, the message line, holds nothing but spaces. Then msg("") -- which
 * describe() says on every plain step -- has nothing to write: on the 128K
 * that was 32 cell blits a step for a line that was already blank. Text
 * reaching row 0 clears it (print_str, put_uint: every writer of the row goes
 * through one of them); blanking the row sets it (clear_line, tm_cls). The
 * Next rewrites its 80 cells at 28 MHz in well under a millisecond, which
 * is not worth the resident bytes the bookkeeping costs there. */
static uint8_t msg_clear;
#endif

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

/* Each tile's base attribute, ink | paper<<3; draw_map ORs in BRIGHT for
 * in-sight cells. Every map path takes the whole byte, so a tile may have a
 * paper: the walls are white bricks on blue (15), a door a red plank framed
 * in black (16) and a shop's walls yellow on red (22) -- the door lies in
 * either wall and in the mines' yellow-on-black, so black ink is the one
 * frame that stands out in all three (yellow sank into the shop's bricks)
 * -- and the fountain is cyan on blue water (13). Floors, items and
 * monsters stay on black. The 1-bit shapes (udg_bitmap, built from
 * udg_src[] by build_udgs) live in Bank 5, not resident -- see the #define
 * in platform.h. */
const uint8_t udg_ink[NTILES] = {
    /* ROCK   FLOOR  WALL   CORR   DOOR   SUP    SDOWN */
       0,     7,     15,    7,     16,    7,     7,
    /* HERO   DOG    RAT    GOLD   FOOD   DOLLAR WEAPON ARMOR  POTION */
       5,     6,     7,     6,     2,     4,     7,     7,     5,
    /* KOBOLD ORC    SNAKE  BAT    ZOMBIE SCROLL RING   AMULET ACIDBLOB */
       6,     4,     4,     1,     7,     7,     6,     6,     4,  /* bat: blue */
    /* SHOPWALL KEEPER  LEPRECHAUN YELLOWLIGHT TRAP HOMUNCULUS WRAITH ALTAR WAND FEYE BOOK FOUNTAIN */
       22,      3,          4,          6,        2,      2,        7,     7,    5,   1,   2,   13,
    /* TROLL  VAMPIRE  DRAGON  PRIEST  MIMIC  MINEHOLE GNOME DWARF LUCKSTONE MINEWALL NYMPH TOOL */
       6,     7,       2,      3,      3,     6,       1,    7,    7,        6,       3,    6,
    /* SINK  THRONE  GRAVE (1.8) */
       7,    22,     7,
    /* UNICORN ROTHE QMECH RUSTMON OGRE SPIDER UMBERHULK PUDDING HILLGIANT LICH (1.9):
     * the unicorn and the lich white, the brown ones yellow (NetHack's brown,
     * as ever), the mechanic and the giant cyan, the rust monster red, the
     * spider magenta, the pudding blue -- black ink would vanish on black */
       7, 6, 5, 2, 6, 3, 6, 1, 5, 7
    /* sink and grave white; the throne yellow on red, the Next's gold frame
     * round a red cushion (the shop wall's pair, which no room floor wears) */
    /* zombie white (NetHack's human zombie); keeper magenta; leprechaun green; yellow light
     * yellow; trap+imp red; wraith+altar white; wand cyan; eye blue; book red; fountain cyan on blue;
     * troll yellow (NetHack's brown, as the Next draws it); vampire white; dragon red;
     * high priest + mimic magenta; mine hole yellow; gnome blue (the Next's smock);
     * dwarf + luckstone white; nymph magenta (the charm reads at a glance); tool
     * yellow (the Next's brown handle). Seven
     * monsters were green, three of them humanoids sharing the same floors (orc,
     * zombie, troll); four are now. */
};

/* The three ULA cell blits -- putcell (text, from font_ram), puttile (a UDG tile
 * drawn "in hand"), and puttile_attr (a UDG map tile with explicit fog-of-war
 * attribute) -- all copy an 8-byte glyph into the bitmap (pixel rows +0x100
 * apart) and set the cell attribute. They are the hot render path: the status
 * bar redraws ~64 cells every turn and an edge-scroll redraws the whole
 * ~672-cell viewport, so all three live in hand-written Z80 in
 * src/puttile_asm.asm (declared in platform.h). The cell bitmap address is
 * 0x4000 + (y&0x18)*256 + (y&7)*32 + x; the attribute 0x5800 + (y>>3)*256 +
 * (y&7)*32 + x; text attr = (coff&7)|(BRIGHT?0x40:0), tiles = udg_ink|0x40. */

void tm_cls(void)
{
    uint8_t *p = (uint8_t *)SCRN_BASE;
    uint16_t i;
    for (i = 0; i < 6144; i++) p[i] = 0;          /* blank the bitmap   */
    p = (uint8_t *)ATTR_BASE;
    for (i = 0; i < 768; i++) p[i] = 0;            /* black ink on black */
    msg_clear = 1;
}
#endif  /* __ZXNEXT */

/* ---- text helpers (shared) ---- */

uint8_t print_str(uint8_t x, uint8_t y, const char *s, uint8_t coff)
{
#ifndef __ZXNEXT
    if (!y && *s) msg_clear = 0;
#endif
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

#ifndef __ZXNEXT
    if (!y) msg_clear = 0;
#endif
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
#ifndef __ZXNEXT
    if (!y) msg_clear = 1;
#endif
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

/* esxDOS reports how many bytes it moved, and these used to drop it: a full
 * card's short write was a "Game saved". file_write latches the shortfall
 * and keeps the count and sum save.c seals the file with; file_read hands
 * the count back, which is how the load finds the end of the file. */
uint8_t  file_bad;
uint16_t file_len, file_sum;

void file_write(uint8_t h, const void *src, uint16_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    uint16_t k;
    for (k = 0; k < n; k++) file_sum += s[k];
    file_len += n;
    if (esx_f_write(h, (void *)src, n) != n) file_bad = 1;
}

uint16_t file_read(uint8_t h, void *dst, uint16_t n)
{
    return esx_f_read(h, dst, n);
}

void file_close(uint8_t h)
{
    if (esx_f_close(h)) file_bad = 1;   /* a write's last sectors go out here */
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
#ifndef __ZXNEXT
    if (!*s) {                  /* every plain step says this (describe) */
        if (msg_clear) return;
        pad_eol(0, 0);
        msg_clear = 1;
        return;
    }
#endif
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

/* NetHack's --More-- (1.8): the line on show stays until a key, because
 * another follows it; s, if given, is shown first. Every message keeps to the
 * 128K's 32 columns, so on the Next the prompt fits after it on row 0; the
 * 128K's message row is full, so the prompt borrows the right end of row 1
 * -- the map's top row -- and those cells are put back from VIEW_SHADOW
 * (0x6000, nexthack.c), which always holds what draw_map painted there. */
void msg_more(const char *s)
{
    if (s) msg(s);
#ifdef __ZXNEXT
    print_str(34, 0, "--More--", C_CYAN | C_BRIGHT);
#else
    print_str(24, 1, "--More--", C_CYAN | C_BRIGHT);
#endif
    in_wait_nokey();
    getkey();
    in_wait_nokey();
#ifndef __ZXNEXT
    {
        const uint8_t *sh = (const uint8_t *)0x6000u + 48;   /* map row 0, col 24 */
        uint8_t x;
        for (x = 24; x < 32; x++, sh += 2) puttile_attr(x, 1, sh[0], sh[1]);
    }
#endif
}

/* Unpack one stream (tools/lztmpl.py wrote it) straight into its destination
 * -- lvl[][] for a template (leveltmpl.c), the ULA screen for the 128K's title
 * and victory SCRs (scr.c). RESIDENT since 1.8 so that it reads the stream
 * from whichever bank the caller runs in: the data stays beside its reader.
 *
 * There is no window buffer and none is needed: a match points back into the
 * OUTPUT, and the 1680 bytes load_template was always going to write ARE the
 * window. That is the whole reason compressing this paid -- the destination
 * already existed, so 8400 B of grids became 864 B of bank and cost no RAM.
 *
 * Copying a byte at a time is deliberate, not lazy: a match may overlap the
 * write head (offset 1 means "repeat the last byte"), which is what makes the
 * long runs of rock and floor nearly free. A memcpy would read past what has
 * been written. */
void lz_expand(const uint8_t *in, char *out, uint16_t size)
{
    uint16_t done = 0;
    uint8_t ctrl = 0, bit = 0;

    while (done < size) {
        if (bit == 0) { ctrl = *in++; bit = 8; }
        if (ctrl & 1) {
            out[done++] = (char)*in++;
        } else {
            uint8_t b0 = *in++;
            uint8_t b1 = *in++;
            /* offset is stored biased by one, so all 11 bits are usable */
            uint16_t off = ((uint16_t)b0 | ((uint16_t)(b1 >> 5) << 8)) + 1u;
            uint8_t n = (uint8_t)((b1 & 0x1Fu) + 3u);
            do { out[done] = out[done - off]; done++; } while (--n);
        }
        ctrl >>= 1;
        bit--;
    }
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

/* getkey with typematic repeat, for the turn loop. A fresh press acts at once;
 * a key still held since the last returned press repeats only after RPT_FIRST
 * frames, then every RPT_NEXT (the 50 Hz FRAMES sysvar -- wall clock on both
 * the 3.5 MHz 128K and the 28 MHz Next). Needed because in_pause() returns AT
 * ONCE while any key is down, so nothing ever throttled a held key: once the
 * per-turn work got fast, a ~120 ms human tap fired 2-3 moves and you couldn't
 * stop on the cell you wanted. Tap = exactly one step; hold = one step, a
 * beat, then a steady walk. Release at any point re-arms the instant path. */
#ifdef __ZXNEXT
#define RPT_FIRST 10            /* ~200 ms before the first repeat: long enough
                                 * that a human tap stays exactly one step */
#define RPT_NEXT   2            /* ~50 ms between repeats: the held walk on the
                                 * 28 MHz Next -- brisk without the pre-typematic
                                 * runaway (raw turn ~40 ms = 25 cells/s) */
#define RPT_GUARD 370           /* the bare .nex runs with interrupts off, so
                                 * FRAMES never ticks: the poll loop IS the
                                 * timer. One poll iteration ~= 1500 T (zxn
                                 * in_inkey scans the extended matrix), so 370
                                 * polls at 28 MHz ~= 20 ms, one nominal frame
                                 * -- calibrated from a measured 1.2 s hold. */
#else
/* On the 128K the beat is counted from the moment the previous step was
 * HANDED OUT (`given`), not from when the loop comes back for the next key:
 * the turn that step bought -- move, monsters, redraw -- has already spent
 * part of the beat, often all of it at 3.5 MHz. Counted from the return, the
 * whole beat landed on top of the turn: ~80 ms of turn + ~70 ms of beat put
 * a held walk at 150 ms a step in a room and 180 in a corridor, where the
 * Next walks at ~107 ms (both measured in MAME). Anchored, a held key steps
 * every max(turn, beat). A count of n FRAMES ticks is (n-1, n] frames. */
#define RPT_FIRST 11            /* 200-220 ms before the first repeat (the
                                 * Next measures 212): a ~120 ms human tap
                                 * stays one step */
#define RPT_NEXT   5            /* 80-100 ms a step after it, the Next's pace */
#define RPT_GUARD 4000          /* polls per frame: only a backstop, in case
                                 * FRAMES ever stops ticking */
#endif

/* A modal cursor (farlook) glides too fast at the Next's brisk walk repeat:
 * while one is up the repeat drops to ~100 ms -- which is the 128K's walking
 * beat already, so slowing is a no-op there. */
#ifdef __ZXNEXT
#define RPT_SLOW 5      /* ~100 ms: 4 (~80 ms) felt rushed, 7 (~140 ms) sluggish */
#else
#define RPT_SLOW RPT_NEXT
#endif
static uint8_t rpt_next = RPT_NEXT;
void key_rpt_slow(uint8_t on) { rpt_next = on ? RPT_SLOW : RPT_NEXT; }

int getkey_rpt(void)
{
    static int last = 0;
    static uint8_t rpt = 0;
#ifndef __ZXNEXT
    static uint8_t given;               /* FRAMES when `last` was handed out */
#endif
    volatile uint8_t *fr = (volatile uint8_t *)23672;   /* FRAMES lsb, 50 Hz */
    int k = in_inkey();
    if (k != 0 && k == last) {          /* the same key is still held */
        uint8_t n = rpt ? rpt_next : RPT_FIRST;
#ifdef __ZXNEXT
        while (n--) {
            uint8_t  f = *fr;
            uint16_t guard = RPT_GUARD;
            while (*fr == f && --guard) {
                int kk = in_inkey();
                if (kk != 0 && kk != last) {   /* rolled to a NEW key: act now */
                    last = kk; rpt = 0; return kk;
                }
                /* a momentary 0 is NOT a release -- one keyboard scan can miss
                 * a held key (matrix / debounce). Ignoring it is the fix for
                 * the crawl: treating it as a release reset rpt, so every step
                 * restarted at the long RPT_FIRST delay instead of RPT_NEXT. */
            }
        }
#else
        uint16_t guard = (uint16_t)(RPT_GUARD * n);
        while ((uint8_t)(*fr - given) < n && --guard) {  /* see RPT_FIRST */
            int kk = in_inkey();
            if (kk != 0 && kk != last) {       /* rolled to a NEW key: act now */
                last = kk; rpt = 0; given = *fr; return kk;
            }
            /* a momentary 0 is not a release (see the Next's loop above) */
        }
#endif
        /* still down? require two 0s in a row before believing a release, so a
         * single missed scan can't drop us back to the slow first-repeat beat */
        if (in_inkey() != 0 || in_inkey() != 0) {
            rpt = 1;
#ifndef __ZXNEXT
            given = *fr;
#endif
            return last;
        }
    }
    last = 0; rpt = 0;
    do { k = in_inkey(); } while (k == 0);   /* block for a fresh press */
    last = k;
#ifndef __ZXNEXT
    given = *fr;
#endif
    return k;
}
