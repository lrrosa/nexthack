SECTION PAGE_20_CODE
    ORG 0x14C000
SECTION PAGE_22_CODE
    ORG 0x16C000
; The Layer 2 title image (titlegfx0/1/2.c) goes in banks 16/17/18 via z88dk's
; predefined BANK_16/17/18 sections (already ORG'd at 0x20C000/0x22C000/0x24C000
; = the 0xC000 window over those banks), so nothing to add here.
