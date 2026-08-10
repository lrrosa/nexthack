/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* music.h - the AY title theme (music.c, banked). The game's other sound is
 * the beeper (sfx.c); this is the only use of the AY chip. */
#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>

/* Play the title theme, returning when a key is pressed. The return value is
 * the entropy gathered while waiting -- the title has always seeded the world
 * from the player's reaction time, and this preserves it (see music.c). */
uint16_t music_title_wait(void) __banked;

/* Cut all three channels (music_title_wait does this on the way out). */
void music_silence(void) __banked;

#endif /* MUSIC_H */
