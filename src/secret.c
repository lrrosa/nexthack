/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* secret.c - secret doors and secret corridors (1.8), and searching for them.
 *
 * NetHack hides one door in eight behind a stretch of wall, and the odd
 * corridor square behind rock, and 's' is mostly for finding them. Here the
 * choice is a side hash per cell -- never rn2, so generation, spawning and
 * every persistence index see the level exactly as before -- applied by
 * build_level right after gen_level. Ordinary levels only, from Dlvl 2: the
 * Big Room has no doors, the hand-drawn maps are drawn as they are meant to
 * be, and the mines are hewn, not built.
 *
 * ONE RULE NetHack does not keep: a secret never cuts the way through the
 * level. A flood from the up stairs marks one way to the down stairs (the
 * Amulet's cell on Dlvl 50 -- the same cell) and to the mine entrance on
 * Dlvl 2, and nothing on it hides. NetHack can wall
 * you in, and 's' gets you out at one chance in seven a turn; on a Spectrum,
 * searching every wall of a dead level for the stairs is not a game. Side
 * rooms, a vault, a branch of the corridor -- those can hide. The fuzzer's
 * walk check and the attract demo's walk to '>' both lean on this.
 *
 * A door hides only where door_hash (nexthack.c) has (h & 7) == 2, and a door
 * is LOCKED only where (h & 3) == 0, so no door is ever both: a found secret
 * door is never locked, and the locked doors' map-order index -- which
 * door_open's bits name -- never shifts when one is found.
 *
 * Found ones are remembered as dug cells are (levelfov.c dug_add): a passage
 * as a tunnel, a door with DUG_DOOR. The current level's hidden cells are
 * listed here (sec_*) for 's' to try; build_level rebuilds the list on every
 * entry, before dug_restore re-opens the found ones, so 's' checks that a
 * listed cell is still hidden before it "finds" it. */

#include <string.h>       /* memset */
#include "level.h"
#include "game.h"
#include "rng.h"
#include "item.h"         /* eff_luck */
#include "nexthack.h"     /* door_hash */

#define SEC_MAX 6
static uint8_t sec_n, sec_x[SEC_MAX], sec_y[SEC_MAX];

/* The flood borrows the monster BFS's scratch in Bank 5 (monster_ai.c's dist
 * and bfsq, 0x7400-0x7C8F): a level is being built, so no field there is
 * current -- every reader floods afresh before it reads (compute_dist_map,
 * dist_map_from). The Next lays it out as monster_ai does, the labels at
 * 0x7400 and a ring of uint16 behind them; the 128K's Z80 flood (lw_run)
 * keeps its ring as two page-aligned byte planes at 0x7400 and 0x7500 and
 * the labels at 0x7600, which end at 0x7C90 exactly. */
#ifdef __ZXNEXT
#define seen ((uint8_t *)0x7400u)
#define ring ((uint16_t *)0x7A90u)
#else
#define seen ((uint8_t *)0x7600u)
#endif
#define ON_WAY 255            /* seen[] mark of a cell on the kept way through */

/* walkable(), inline: this is the cost of entering a level */
#define OPEN(c) ((c) != '|' && (c) != '-' && (c) != ' ')

/* ONE flood per level, from '<', four ways round (whatever it reaches, the
 * hero can): seen[k] = the step count + 1, 0 = not reached. It stops once the
 * targets -- '>' (the Amulet's cell on Dlvl 50) and the mine entrance on
 * Dlvl 2 -- are labelled. 0 if one is not reached, or lies too far for a
 * byte, or the ring fills: then nothing hides, which is always safe. */
#ifdef __ZXNEXT
/* The base pointers sit a row above the cell so every offset is non-negative
 * (CLAUDE.md: a pointer plus a negative constant can assemble wrong). */
#define TRY(off)                                                        \
    if (!sb[off] && OPEN(lb[off])) {                                    \
        sb[off] = nd;                                                   \
        if ((uint8_t)(tail + 1) == head) return 0;                      \
        ring[tail++] = (uint16_t)(k - MAPW + (off));                    \
    }
static uint8_t label_way(uint16_t dn, uint16_t mn)
{
    uint16_t k;
    uint8_t head = 0, tail = 0, nd, left = (uint8_t)(mn == dn ? 1 : 2);
    uint8_t *sb;
    const char *lb;

    memset(seen, 0, (uint16_t)(MAPH * MAPW));
    k = (uint16_t)up_y * MAPW + up_x;
    seen[k] = 1;
    ring[tail++] = k;
    while (head != tail) {
        k = ring[head++];
        if ((k == dn || k == mn) && !--left) return 1;
        nd = (uint8_t)(seen[k] + 1);
        if (nd >= ON_WAY - 1) return 0;     /* past a byte's reach: give up */
        /* walkable cells never touch the map's edge on a generated level,
         * so all four neighbours are on the map */
        sb = seen + (k - MAPW);
        lb = (const char *)lvl + (k - MAPW);
        TRY(0) TRY(MAPW - 1) TRY(MAPW + 1) TRY(2 * MAPW)
    }
    return 0;
}
#undef TRY
#else
/* +zx: the same flood in hand-written Z80, as monster_ai.c keeps its
 * flood_run. In C (the Next's, above) it cost the 128K 50 ms a level,
 * measured in MAME over nine descents; this costs 23, 5.6 of them clearing
 * the field. It lives here, inline, so that it is always in this module's
 * bank, whichever banks.json gives it.
 *
 * It is the C step for step, and leaves seen[] byte for byte as the C does:
 * the same neighbour order (up, left, right, down), the same tests, the same
 * two give-ups (a label past 253; a 256th cell for the ring, which holds 255)
 * and the same early exit -- when the last target is TAKEN from the ring, not
 * when it is labelled, so the cells labelled in between are labelled here too.
 * The ring changed shape, not order: slot i's low byte at 0x7400+i, its high
 * byte at 0x7500+i, so head and tail are bytes that wrap by themselves.
 *
 * Targets: label_way (below) puts a 0 on each target's lvl[] cell for the
 * flood, and puts the map back. lvl[] holds printable characters only, and a
 * 0 passes the walkable test as the stairs under it did. So "is this a
 * target" is one byte of the cell being expanded, where comparing the cell
 * with two targets cost half as much again.
 *
 * In: lw_s = &seen[up], lw_left = the targets to find. Out: L = 1 or 0.
 * Every queue entry is a seen[] address; IX -> lvl[] of the cell expanded,
 * HL walks seen[] round it, DE -> the tail slot (D = the low plane), BC = 79
 * (one neighbour to the next), the other A = the label for its new neighbours. */
static uint16_t lw_s;
static uint8_t  lw_left, lw_head;

static uint8_t lw_run(void) __naked
{
    __asm
LW_SEEN     equ 0x7600              ; seen[], as #defined above
LW_RING     equ 0x74                ; the ring: low bytes at 0x7400 + slot,
                                    ; high bytes at 0x7500 + slot

; One neighbour, at seen (HL) and lvl (IX+OFF): the C TRY.
LW_TRY MACRO OFF, SKIP
    ld   a, (hl)
    or   a
    jr   nz, SKIP                   ; labelled already
    ld   a, (ix+OFF)                ; its map char: walkable?
    cp   0x20                       ; rock
    jr   z, SKIP
    cp   0x2d                       ; -
    jr   z, SKIP
    cp   0x7c                       ; |
    jr   z, SKIP
    ex   af, af
    ld   (hl), a                    ; label it
    ex   af, af
    ld   a, l                       ; queue it
    ld   (de), a
    inc  d
    ld   a, h
    ld   (de), a
    dec  d
    inc  e
    ld   a, (_lw_head)
    cp   e
    jp   z, lw_ret0                 ; that was a 256th: the ring is full
SKIP:
    ENDM

    push ix
    ld   hl, LW_SEEN                ; seen[] = 0, 1680 = 105 x 16 bytes: from
    xor  a                          ; a page boundary, so inc l never wraps
    ld   b, 105                     ; inside a run of 16
lw_clr:
    REPT 15
    ld   (hl), a
    inc  l
    ENDR
    ld   (hl), a
    inc  hl
    djnz lw_clr

    ld   hl, (_lw_s)                ; seen[up] = 1, and up is queued
    ld   (hl), 1
    ld   de, LW_RING * 256          ; DE -> slot 0
    ld   a, l
    ld   (de), a
    inc  d
    ld   a, h
    ld   (de), a
    dec  d
    inc  e                          ; tail = 1
    xor  a
    ld   (_lw_head), a              ; head = 0
    ld   bc, 79
    jr   lw_next
lw_hit:                             ; (here so that the loop reaches it by jr)
    ld   a, (_lw_left)
    dec  a
    ld   (_lw_left), a
    jr   nz, lw_exp                 ; not the last: expand it like any cell
    ld   l, 1
    jr   lw_ret
lw_ret0:
    ld   l, 0
lw_ret:
    pop  ix
    ret
lw_next:
    ld   a, (_lw_head)
    cp   e
    jr   z, lw_ret0                 ; the ring ran dry
    ld   l, a
    inc  a
    ld   (_lw_head), a
    ld   h, LW_RING
    ld   a, (hl)
    inc  h
    ld   h, (hl)
    ld   l, a                       ; HL -> seen[k]
    ld   ix, _lvl - LW_SEEN
    ex   de, hl
    add  ix, de                     ; IX -> lvl[k]
    ex   de, hl
    ld   a, (ix+0)
    or   a
    jr   z, lw_hit                  ; a target
lw_exp:
    ld   a, (hl)
    inc  a
    cp   254
    jr   nc, lw_ret0                ; past a byte reach: give up
    ex   af, af                     ; the other A = the label of the next step
    or   a
    sbc  hl, bc
    dec  hl                         ; HL -> seen[k - 80]
    LW_TRY -80, lw_s1
    add  hl, bc
    LW_TRY -1, lw_s2
    inc  hl
    inc  hl
    LW_TRY 1, lw_s3
    add  hl, bc
    LW_TRY 80, lw_s4
    jp   lw_next
    __endasm;
}

static uint8_t label_way(uint16_t dn, uint16_t mn)
{
    char    *td = &lvl[0][0] + dn, *tm = &lvl[0][0] + mn;
    char     cd = *td, cm = *tm;
    uint16_t k = (uint16_t)up_y * MAPW + up_x;
    uint8_t  r;

    lw_left = (uint8_t)(mn == dn ? 1 : 2);
    lw_s = (uint16_t)seen + k;
    /* mark each target the C flood would take from the ring: one it can
     * label, or up itself, which is queued without a test */
    if (OPEN(cd) || dn == k) *td = 0;
    if (OPEN(cm) || mn == k) *tm = 0;
    r = lw_run();
    *td = cd;
    *tm = cm;
    return r;
}
#endif

/* Walk back from target t to '<', each step to a neighbour one closer, and
 * mark the way ON_WAY. A walk that meets a way already marked stops there. */
static void mark_way(uint16_t t)
{
    uint8_t d = seen[t];
    seen[t] = ON_WAY;
    while (d > 1) {
        d--;
        if      (seen[t - MAPW] == d) t -= MAPW;
        else if (seen[t + MAPW] == d) t += MAPW;
        else if (seen[t - 1] == d)    t -= 1;
        else if (seen[t + 1] == d)    t += 1;
        else return;                        /* joined the other target's way */
        seen[t] = ON_WAY;
    }
}

/* a straight stretch of corridor: '#' both ways along it, rock both sides */
static uint8_t mid_corridor(uint8_t x, uint8_t y)
{
    return (uint8_t)((lvl[y][x - 1] == '#' && lvl[y][x + 1] == '#' &&
                      lvl[y - 1][x] == ' ' && lvl[y + 1][x] == ' ') ||
                     (lvl[y - 1][x] == '#' && lvl[y + 1][x] == '#' &&
                      lvl[y][x - 1] == ' ' && lvl[y][x + 1] == ' '));
}

/* One flood finds a way from '<' to each target and marks it; then the first
 * SEC_MAX cells the hashes pick OFF that way are hidden. The way stays whole,
 * so the stairs stay joined however many hide -- by construction, with no
 * flood per candidate. (A flood per candidate made entering a level on the
 * 128K take up to 1.03 s, measured in MAME, where 1.7.0 took 0.24-0.29.) The
 * scan finds each row's doors and corridor squares with memchr (CPIR, in
 * the library's assembly) and looks at nothing else: a C loop over all 1482
 * cells cost the 128K ~70 ms a level. */
void secrets_hide(void) __banked
{
    uint8_t x, y;
    uint16_t dn, mn, hrow;
    sec_n = 0;
    if (dlvl < 2 || IN_MINES(dlvl) || level_is_special()) return;
    dn = (uint16_t)dn_y * MAPW + dn_x;
    mn = (dlvl == MINES_ENTR_DLVL) ? (uint16_t)mn_y * MAPW + mn_x : dn;
    if (!label_way(dn, mn)) return;
    mark_way(dn);
    if (mn != dn) mark_way(mn);

    hrow = (uint16_t)(world_seed * 13u + (uint16_t)dlvl * 977u + 211u);
    for (y = 1; y < MAPH - 1; y++, hrow += 211u) {
        char          *row = &lvl[y][0];
        const uint8_t *srow = seen + (uint16_t)y * MAPW;
        uint8_t        pass;
        for (pass = 0; pass < 2; pass++) {          /* the doors, then the corridors */
            char  want = pass ? '#' : '+';
            char *p = row + 1, *q;
            while ((q = memchr(p, want, (uint16_t)(row + (MAPW - 1) - p))) != 0) {
                char hid;
                p = q + 1;
                x = (uint8_t)(q - row);
                if (srow[x] == ON_WAY) continue;
                if (!pass) {
                    /* one door in eight, as NetHack's dodoor -- never a
                     * shop's: its keeper must be able to leave, and a shop
                     * you have to search for is no shop */
                    if ((door_hash(x, y) & 7u) != 2 || shop_in_room(x, y)) continue;
                    hid = (row[x - 1] == '-' || row[x + 1] == '-') ? '-' : '|';
                } else {
                    /* the odd square of a straight corridor, about one in
                     * 64: the dead end that says "search here" */
                    uint16_t m = (uint16_t)(hrow + (uint16_t)x * 59u);
                    m ^= (uint16_t)(m << 7);
                    m ^= (uint16_t)(m >> 9);
                    if ((m & 63u) != 5 || !mid_corridor(x, y)) continue;
                    hid = ' ';
                }
                *q = hid;
                sec_x[sec_n] = x; sec_y[sec_n] = y;
                if (++sec_n == SEC_MAX) return;
                if (p == row + (MAPW - 1)) break;
            }
        }
    }
}

/* Open listed secret i for good: the map, and the dug pool that remembers it.
 * 1 for a door, 2 for a passage, 0 if it is not hidden any more (found on an
 * earlier visit, or dug through). */
static uint8_t reveal(uint8_t i)
{
    uint8_t x = sec_x[i], y = sec_y[i];
    char *c = &lvl[y][x];
    if (*c == ' ') {
        *c = '#';
        dug_add(x, y);
        map_flush = 1;
        return 2;
    }
    if (*c == '|' || *c == '-') {
        *c = '+';
        dug_add(x, (uint8_t)(y | DUG_DOOR));
        map_flush = 1;
        return 1;
    }
    return 0;
}

/* 's': each hidden cell beside you turns up one search in five -- NetHack's
 * one in seven, a little kinder because this game has no count prefix (no
 * "20s"); luck shortens or stretches it, as NetHack's rnl does. Returns the
 * kinds found (1 door, 2 passage). */
uint8_t secrets_search(void) __banked
{
    uint8_t i, found = 0, odds;
    int8_t lk = eff_luck();
    odds = (uint8_t)(lk > 0 ? 3 : lk < 0 ? 9 : 5);
    for (i = 0; i < sec_n; i++) {
        int dx = (int)sec_x[i] - hero_x, dy = (int)sec_y[i] - hero_y;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) continue;
        if (rn2(odds)) continue;
        found |= reveal(i);
    }
    return found;
}

/* A blessed scroll of magic mapping shows the secret doors for what they are,
 * as NetHack's does; the passages stay rock. */
void secrets_reveal_doors(void) __banked
{
    uint8_t i;
    for (i = 0; i < sec_n; i++)
        if (lvl[sec_y[i]][sec_x[i]] != ' ') reveal(i);
}

/* A restored game puts the hero back on the saved square -- and a save
 * made before 1.8 knows nothing of secrets: that square may be a doorway
 * that is wall now. Open whatever is underfoot (mainentry.c, after
 * build_level). */
void secrets_underfoot(void) __banked
{
    uint8_t i;
    for (i = 0; i < sec_n; i++)
        if (sec_x[i] == (uint8_t)hero_x && sec_y[i] == (uint8_t)hero_y) reveal(i);
}
