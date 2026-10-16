/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* scr.c - BANKED blitter for the title/victory SCR loading screens.
 *
 * It MUST live in the same bank as the SCR consts (title_scr.c / victory_scr.c
 * -- banks.json enforces that as a 'colocate' group) so the const is mapped
 * into the 0xC000 window when blit() reads it -- a banked const cannot be
 * reached from code running in another bank. The
 * copy target is the resident ULA screen at 0x4000 (6144 bitmap + 768 attrs).
 * Reached via the trampoline from title_screen / victory_screen.
 *
 * Since 1.8 the screens are LZ-packed (tools/png2scr.py, tools/lztmpl.py's
 * format) and unpacked straight onto the screen by the resident lz_expand,
 * which reads its back-references from the screen it is writing: 13.8 KB of
 * SCR became ~7 KB, the room the 1.8 furniture needed in the 128K's banks. */

#include "scr.h"
#include "platform.h"     /* lz_expand */
#include <stdint.h>

extern const uint8_t title_scr[];
extern const uint8_t victory_scr[];

static void blit(const uint8_t *s)
{
    lz_expand(s, (char *)0x4000u, 6912);
}

void show_title_scr(void)   __banked { blit(title_scr);   }
void show_victory_scr(void) __banked { blit(victory_scr); }
