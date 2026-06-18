/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* scr.c - BANKED (BANK_4) blitter for the title/victory SCR loading screens.
 *
 * It MUST live in the same bank as the SCR consts (title_scr.c / victory_scr.c,
 * also BANK_4) so the const is mapped into the 0xC000 window when blit() reads
 * it -- a banked const cannot be reached from code running in another bank. The
 * copy target is the resident ULA screen at 0x4000 (6144 bitmap + 768 attrs).
 * Reached via the trampoline from title_screen / victory_screen. */

#include "scr.h"
#include <stdint.h>

#pragma codeseg BANK_4

extern const uint8_t title_scr[];
extern const uint8_t victory_scr[];

static void blit(const uint8_t *s)
{
    uint8_t *d = (uint8_t *)0x4000u;
    uint16_t i;
    for (i = 0; i < 6912; i++)
        d[i] = s[i];
}

void show_title_scr(void)   __banked { blit(title_scr);   }
void show_victory_scr(void) __banked { blit(victory_scr); }
