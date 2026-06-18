/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* scr.h - show the const-banked title/victory SCR loading screens (+zx). */
#ifndef SCR_H
#define SCR_H

void show_title_scr(void)   __banked;   /* blit tools/title.png   -> the ULA */
void show_victory_scr(void) __banked;   /* blit tools/victory.png -> the ULA */

#endif /* SCR_H */
