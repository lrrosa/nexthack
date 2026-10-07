; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Leonardo Roman da Rosa
;
; puttile_asm.asm - the +zx (ULA) cell blits in hand-written Z80.
;
; putcell (text: status bar ~64 cells every turn, messages, help), puttile and
; puttile_attr (map tiles: a whole ~672-cell viewport on an edge-scroll redraw)
; all copy an 8-byte glyph to the ULA (pixel rows are 0x100 apart -> `inc d`)
; and set the attribute. The C versions looped with an array index + counter;
; these share a tight `ld a,(hl)/ld (de),a/inc hl/inc d/djnz` core (~2x/cell),
; smoothing both the per-turn status redraw and the edge-scroll.
;
; dist_clear is the same idea for the monster-BFS scratch map: a tight unrolled
; fill of the 1680-byte dist[] that the chase BFS resets every turn (its biggest
; per-turn cost on the 3.5 MHz 128K).
;
; dm_row is draw_map's full-path terrain sweep for one viewport row: the C loop
; it replaced cost ~2500 T a cell, which made every edge-scroll a 0.6 s stall.
;
; +zx only (the Next build keeps its tilemap path and never links this).

    SECTION code_compiler
    PUBLIC  _putcell, _puttile, _puttile_attr, _dist_clear, _dm_row
    PUBLIC  _dmr_lrow, _dmr_seen, _dmr_vis, _dmr_mon, _dmr_shad
    PUBLIC  _dmr_k, _dmr_y, _dmr_shop0, _dmr_shopn, _dmr_force
    EXTERN  _udg_ink

UDG_BITMAP equ 0x6680      ; tile shapes in Bank 5 (always mapped); see platform.h

ROM_FONT equ 0x3c00

; ---- shared tail: HL = src glyph, A = attribute, frame already set up via IX ----
; cell  bitmap addr = 0x4000 + (y&0x18)*256 + ((y&7)*32 + x)
; cell  attr   addr = 0x5800 + (y>>3)*256   + ((y&7)*32 + x)   (same low byte)
; x = (ix+4), y = (ix+5).  Caller did `push ix / ld ix,0 / add ix,sp`.
pta_core:
    ld   c, a               ; C = attribute
    ld   a, (ix+5)          ; y
    and  7
    rrca
    rrca
    rrca                    ; (y&7) << 5
    add  a, (ix+4)          ; + x   -> shared low byte
    ld   e, a
    ld   a, (ix+5)          ; y
    rrca
    rrca
    rrca
    and  0x1f               ; y >> 3
    add  a, 0x58
    ld   d, a               ; DE = attribute address
    ld   a, c
    ld   (de), a            ; store the attribute
    ld   a, (ix+5)          ; y
    and  0x18
    add  a, 0x40
    ld   d, a               ; DE = cell bitmap address (E = low byte, unchanged)
    ; 8-row copy, fully unrolled. The dest stride is +0x100 (next pixel row),
    ; so HL and DE never advance together -> LDI/LDIR are out; we inc each. No
    ; loop counter: dropping the per-row djnz is ~109 t-states/cell, and this
    ; core is shared by all three blits so it costs the ~40 bytes only once.
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)            ; row 7: last copy, no further advance needed
    ld   (de), a
    pop  ix
    ret

; void putcell(uint8_t x, uint8_t y, uint8_t ch, uint8_t coff)
;   ch=(ix+6), coff=(ix+7); src = ROM font; attr = (coff&7) | ((coff&8)<<3)
_putcell:
    push ix
    ld   ix, 0
    add  ix, sp
    ld   a, (ix+6)          ; ch
    ld   l, a
    ld   h, 0
    add  hl, hl
    add  hl, hl
    add  hl, hl             ; ch * 8
    ld   bc, ROM_FONT
    add  hl, bc             ; HL = src (ROM glyph)
    ld   a, (ix+7)          ; coff
    and  8
    add  a, a
    add  a, a
    add  a, a               ; (coff&8) << 3  = 0 or 0x40 (BRIGHT)
    ld   c, a
    ld   a, (ix+7)
    and  7
    or   c                  ; A = attribute
    jp   pta_core

; void puttile(uint8_t x, uint8_t y, uint8_t tile)
;   tile=(ix+6); src = udg_bitmap; attr = udg_ink[tile-0x80] | 0x40 (BRIGHT)
_puttile:
    push ix
    ld   ix, 0
    add  ix, sp
    ld   a, (ix+6)          ; tile
    sub  0x80               ; index = tile - T_ROCK
    ld   l, a
    ld   h, 0
    ld   bc, _udg_ink
    add  hl, bc
    ld   a, (hl)
    or   0x40               ; A = ink | BRIGHT
    ld   e, a               ; stash attribute in E across the src maths
    ld   a, (ix+6)
    sub  0x80
    ld   l, a
    ld   h, 0
    add  hl, hl
    add  hl, hl
    add  hl, hl             ; index * 8
    ld   bc, UDG_BITMAP
    add  hl, bc             ; HL = src
    ld   a, e               ; A = attribute
    jp   pta_core

; void puttile_attr(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr)
;   tile=(ix+6), attr=(ix+7); src = udg_bitmap; attr passed straight through
_puttile_attr:
    push ix
    ld   ix, 0
    add  ix, sp
    ld   a, (ix+6)          ; tile
    sub  0x80
    ld   l, a
    ld   h, 0
    add  hl, hl
    add  hl, hl
    add  hl, hl             ; index * 8
    ld   bc, UDG_BITMAP
    add  hl, bc             ; HL = src
    ld   a, (ix+7)          ; A = attr
    jp   pta_core

; void dist_clear(uint8_t *p)
;   Fill p[0..1679] with UNREACH (0xFF): the monster-BFS dist[] reset, its
;   biggest per-turn cost when monsters are awake (1680 cell-writes vs the
;   flood's ~112). 1680 = MAPH*MAPW (21*80), unrolled x16 -> 105 djnz passes.
;   Keep the 1680 here in sync with monster_ai.c's dist[] / MAPH*MAPW.
_dist_clear:
    push ix
    ld   ix, 0
    add  ix, sp
    ld   l, (ix+4)
    ld   h, (ix+5)         ; HL = p (dist[])
    ld   a, 0xff           ; UNREACH
    ld   b, 105            ; 1680 / 16 bytes per pass
dc_loop:
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    djnz dc_loop
    pop  ix
    ret

; void dm_row(void)
;   One viewport row (32 cells) of draw_map's FULL path. draw_map has already
;   painted the hero and every visible monster and marked their cells in
;   MON_MAP; this paints the terrain of every other cell, writing a cell only
;   when it differs from VIEW_SHADOW (or always, when dmr_force is set).
;   Parameters (set by draw_map before each call):
;     dmr_lrow  -> lvl[y][vx], the row's map chars
;     dmr_seen  -> the explored bitmap's byte holding cell (vx, y)
;     dmr_vis   -> the same byte of the visible-now bitmap
;     dmr_mon   -> the same byte of MON_MAP
;     dmr_k        that cell's bit in all three (1, 2, 4 .. 128)
;     dmr_shad  -> VIEW_SHADOW's (tile, attr) pair for screen column 0
;     dmr_y        the screen row (OY + y)
;     dmr_shop0, dmr_shopn: the shop's walls, as screen columns shop0 ..
;                  shop0+shopn-1 (shopn 0 = none on this row)
;     dmr_force    1 = rewrite every cell (the shadow was invalidated)
;   The three bitmaps share one layout (bit y*80+x), so one moving mask (E')
;   reads them all; their current bytes ride in B' (explored), C' (visible)
;   and D' (monster). HL' reads DM_CTAB (char -> tile, built by draw_map)
;   and then udg_ink (tile -> ink). The main set: HL -> the map char,
;   IX -> the shadow pair, C = screen column, B = tile, D = attribute.
;   The 48K ROM's IM1 handler (paged in at run time) never touches the
;   alternate set, and IX is put back for the C caller.

DM_CTAB     equ 0x7f80     ; in Bank 5, 128-aligned (nexthack.c's DM_CTAB)
T_ROCK      equ 0x80
T_WALL      equ 0x82
T_SHOPWALL  equ 0x99
T_MINEWALL  equ 0xae

_dm_row:
    push ix
    ld   a, (_dmr_y)        ; the row's screen addresses, as in pta_core
    ld   c, a
    and  0x18
    or   0x40
    ld   (dmr_bhi), a       ; bitmap high byte
    ld   a, c
    and  7
    rrca
    rrca
    rrca
    ld   (dmr_lo), a        ; low byte of column 0 (bitmap and attributes)
    ld   a, c
    rrca
    rrca
    rrca
    and  0x1f
    add  a, 0x58
    ld   (dmr_ahi), a       ; attribute high byte
    exx
    ld   hl, (_dmr_seen)
    ld   b, (hl)
    ld   hl, (_dmr_vis)
    ld   c, (hl)
    ld   hl, (_dmr_mon)
    ld   d, (hl)
    ld   a, (_dmr_k)
    ld   e, a
    ld   h, DM_CTAB / 256
    exx
    ld   ix, (_dmr_shad)
    ld   hl, (_dmr_lrow)
    ld   c, 0               ; screen column
dmr_cell:
    ld   a, (hl)            ; the map char (ASCII, < 128)
    exx
    or   DM_CTAB % 256
    ld   l, a               ; HL' -> its DM_CTAB entry
    ld   a, e
    and  d
    jp   nz, dmr_skip       ; the hero or a monster: draw_map painted it
    ld   a, e
    and  b
    jr   z, dmr_rock        ; never explored: black rock
    ld   a, (hl)            ; the char's tile
    cp   T_WALL
    jr   z, dmr_wall
    cp   T_MINEWALL
    jr   z, dmr_wall
    exx
    ld   b, a               ; B = tile
    exx
dmr_plain_ink:              ; alternate set, A = tile
    sub  T_ROCK
    add  a, +((_udg_ink) & 0xFF)
    ld   l, a
    ld   a, +((_udg_ink) / 256)
    adc  a, 0
    ld   h, a
    ld   a, (hl)            ; its ink
    ld   h, DM_CTAB / 256
dmr_ink:                    ; alternate set, A = ink
    ld   l, a
    ld   a, e
    and  c                  ; in sight right now?
    ld   a, l
    exx                     ; (exx and ld keep the flags)
    jr   z, dmr_have
    or   0x40               ; BRIGHT
    jr   dmr_have

dmr_wall:                   ; alternate set, A = T_WALL or T_MINEWALL
    exx
    ld   b, a               ; B = tile, unless the shop claims the cell
    ld   a, (_dmr_shopn)
    ld   d, a
    ld   a, (_dmr_shop0)
    neg
    add  a, c               ; column - shop0: wraps high left of the shop
    cp   d                  ; a carry = inside its columns (never if shopn 0)
    ld   a, b               ; (ld and exx keep the flags)
    exx
    jr   nc, dmr_plain_ink
    exx
    ld   b, T_SHOPWALL
    exx
    ld   a, (_udg_ink + T_SHOPWALL - T_ROCK)
    jr   dmr_ink

dmr_rock:                   ; alternate set
    exx
    ld   b, T_ROCK
    xor  a                  ; black on black
dmr_have:                   ; main set: B = tile, A = attribute
    ld   d, a
    ld   a, (_dmr_force)
    or   a
    jr   nz, dmr_draw
    ld   a, d
    cp   (ix+1)
    jr   nz, dmr_draw
    ld   a, b
    cp   (ix+0)
    jr   z, dmr_next
dmr_draw:
    ld   (ix+0), b          ; the shadow mirrors the screen
    ld   (ix+1), d
    push hl
    push bc
    ld   a, (dmr_lo)
    add  a, c
    ld   e, a
    ld   l, a
    ld   a, (dmr_ahi)
    ld   h, a
    ld   (hl), d            ; the attribute
    ld   a, (dmr_bhi)
    ld   d, a               ; DE = the cell's top pixel row
    ld   a, b
    sub  T_ROCK
    ld   l, a
    ld   h, 0
    add  hl, hl
    add  hl, hl
    add  hl, hl
    ld   bc, UDG_BITMAP
    add  hl, bc             ; HL = the tile's glyph
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    inc  hl
    inc  d
    ld   a, (hl)
    ld   (de), a
    pop  bc
    pop  hl
dmr_next:                   ; main set
    inc  hl
    inc  ix
    inc  ix
    inc  c
    ld   a, c
    cp   32
    jr   nc, dmr_done
    exx
    rlc  e                  ; the next cell's bit; a carry = the next byte
    jr   nc, dmr_same
    ld   hl, (_dmr_seen)
    inc  hl
    ld   (_dmr_seen), hl
    ld   b, (hl)
    ld   hl, (_dmr_vis)
    inc  hl
    ld   (_dmr_vis), hl
    ld   c, (hl)
    ld   hl, (_dmr_mon)
    inc  hl
    ld   (_dmr_mon), hl
    ld   d, (hl)
    ld   h, DM_CTAB / 256
dmr_same:
    exx
    jp   dmr_cell
dmr_skip:                   ; alternate set
    exx
    jr   dmr_next
dmr_done:
    pop  ix
    ret

    SECTION bss_compiler
_dmr_lrow:  defs 2
_dmr_seen:  defs 2
_dmr_vis:   defs 2
_dmr_mon:   defs 2
_dmr_shad:  defs 2
_dmr_k:     defs 1
_dmr_y:     defs 1
_dmr_shop0: defs 1
_dmr_shopn: defs 1
_dmr_force: defs 1
dmr_lo:     defs 1
dmr_bhi:    defs 1
dmr_ahi:    defs 1
