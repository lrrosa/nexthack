; === ZX Spectrum 128K banking config (see memory zx128-banking-recipe) ===
; Pages 16K banks into 0xC000 via port 0x7FFD (NOT the Next MMU). Cold modules
; mark `#pragma codeseg BANK_N`; each used bank is ORG'd here at its far address
; 0x0NC000 so the banked-call page byte (addr>>16) equals the physical bank N.
; banked_call.asm (vendored from z88dk) supplies the trampoline.
#pragma output REGISTER_SP = 0xBFF0        ; stack just below the 0xC000 paging window

#pragma output CRT_ORG_BANK_0 = 0x00C000   ; item.c's own bank: code + const-banked
                                           ; catalogue/strings (the v0.10 reclaim)
#pragma output CRT_ORG_BANK_1 = 0x01C000   ; cold code bank A
#pragma output CRT_ORG_BANK_3 = 0x03C000   ; cold code bank B
#pragma output CRT_ORG_BANK_4 = 0x04C000   ; data/SCR bank (title/victory, Phase 2)
#pragma output CRT_ORG_BANK_6 = 0x06C000   ; cold code bank C (monster_ai: pet AI overflowed BANK_1)
