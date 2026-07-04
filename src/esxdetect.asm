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
    jr   nz, esxd_api       ; no hardware -> try the API itself (below)
    inc  a                  ; yes -> 1
esxd_done:
    ld   (_esxdos_ok), a
    ret

; No DivMMC hardware answered -- but an emulator's esxDOS TRAPS handler (e.g.
; ZEsarUX's, which serves the API at CPU level without emulating the 0xE3
; paging hardware) may still be present. Probe the API: M_GETSETDRV via RST 8,
; with the ROM's error path caught. On a bare machine RST 8 IS the 48K ROM's
; error restart, which unwinds through ERR_SP (23613): it loads SP from there
; and returns through the address stored on that stack -- so we park a
; recovery address in a private frame, point ERR_SP at it, and a bare ROM
; lands at esxd_caught instead of crashing to BASIC. IY must hold the ROM's
; sysvar base (0x5C3A) during the call: the error routine writes ERR_NR
; through (IY+0), and sdcc_iy uses IY as the C frame pointer -- unsaved, the
; ROM would corrupt a C local.
esxd_api:
    push iy
    ld   (esxd_sp), sp      ; anchor: top of stack = the saved IY
    ld   iy, 0x5C3A         ; ROM sysvar base (ERR_NR = IY+0)
    ld   hl, (23613)
    ld   (esxd_errsp), hl   ; save ERR_SP
    ld   hl, esxd_caught
    push hl                 ; recovery return address for the ROM unwind
    ld   hl, 0
    add  hl, sp
    ld   (23613), hl        ; ERR_SP -> our frame
    xor  a                  ; A=0: query the current drive
    rst  8
    defb 0x89               ; M_GETSETDRV: a served call RETURNS here
    ld   a, 1               ; the API answered -> esxDOS/handler present
    jr   esxd_apidone
esxd_caught:                ; bare ROM: the error restart unwound to here
    xor  a
esxd_apidone:
    ld   sp, (esxd_sp)      ; back to the anchor (saved IY on top) either way
    pop  iy
    ld   hl, (esxd_errsp)
    ld   (23613), hl        ; restore ERR_SP
    ld   (_esxdos_ok), a
    ret

esxd_sp:    defw 0
esxd_errsp: defw 0
