SECTION PAGE_20_CODE
    ORG 0x14C000
SECTION PAGE_22_CODE
    ORG 0x16C000
; PAGE_26_CODE is bank 13 (free): a THIRD code bank for monster_ai, since PAGE_20
; (bank 10) filled up. NOT PAGE_24 (bank 12) -- PAGE_22's const tables already
; spill into bank 12, so a section there collides ("overlaps PAGE_22").
SECTION PAGE_26_CODE
    ORG 0x1AC000
; PAGE_28_CODE is bank 14 (free): item.c's own bank -- its code PLUS its
; const-banked catalogue/strings (the v0.10 reclaim) outgrew PAGE_20.
SECTION PAGE_28_CODE
    ORG 0x1CC000
; The Layer 2 title image (titlegfx0/1/2.c) goes in banks 16/17/18 via z88dk's
; predefined BANK_16/17/18 sections (already ORG'd at 0x20C000/0x22C000/0x24C000
; = the 0xC000 window over those banks), so nothing to add here.
