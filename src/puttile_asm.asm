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
; +zx only (the Next build keeps its tilemap path and never links this).

    SECTION code_compiler
    PUBLIC  _putcell, _puttile, _puttile_attr
    EXTERN  _udg_bitmap, _udg_ink

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
    ld   bc, _udg_bitmap
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
    ld   bc, _udg_bitmap
    add  hl, bc             ; HL = src
    ld   a, (ix+7)          ; A = attr
    jp   pta_core
