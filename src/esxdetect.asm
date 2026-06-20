; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Leonardo Roman da Rosa
;
; esxdetect.asm - probe for a DivMMC / esxDOS on a plain ZX Spectrum 128K.
;
; The save/load code calls the esxDOS API via RST 8. On a machine WITHOUT a
; DivMMC (most 128K emulators and real hardware), RST 8 is the ROM's error
; restart -- so an unguarded esxDOS call drops to BASIC with a garbage error.
; This probes once whether a DivMMC is there; platform.c then skips all file
; I/O (returning FILE_ERR) when it is not, so the game still runs, just without
; save/load. The Next build never links this (NextZXOS is always present).

    SECTION code_compiler
    PUBLIC  _esxdos_detect
    EXTERN  _esxdos_ok

; void esxdos_detect(void);  -- sets _esxdos_ok to 1 (DivMMC present) or 0.
; Port 0xE3 bit 7 (CONMEM) maps the DivMMC EEPROM at 0x0000 and a DivMMC RAM
; bank at 0x2000-0x3FFF. With a DivMMC, 0x2000 becomes writable RAM; without
; one the OUT is ignored and 0x2000 stays ROM, so the test write won't stick.
_esxdos_detect:
    di
    ld   a, 0x80
    out  (0xe3), a          ; CONMEM on, RAM bank 0 -> 0x2000..0x3FFF
    ld   hl, 0x2000
    ld   c, (hl)            ; save the original byte
    ld   a, c
    cpl
    ld   (hl), a            ; write its complement
    ld   e, a               ; = expected if RAM
    ld   a, (hl)            ; read it back
    ld   (hl), c            ; restore the original byte
    ld   d, a               ; = actual
    xor  a
    out  (0xe3), a          ; CONMEM off -> normal memory map
    ei
    ld   a, d
    cp   e                  ; did the complement stick (writable RAM)?
    ld   a, 0
    jr   nz, esxd_done      ; no  -> 0 (no DivMMC/esxDOS)
    inc  a                  ; yes -> 1
esxd_done:
    ld   (_esxdos_ok), a
    ret
