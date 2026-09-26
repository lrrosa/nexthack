/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* attract.h - the title screen's attract demo (attract.c, banked). */
#ifndef ATTRACT_H
#define ATTRACT_H

#include <stdint.h>

/* Three levels of a random dungeon, walked to their down stairs by a hero of
 * a random class and his dog: about ten seconds. A key ends it early and
 * returns the entropy gathered while waiting (never 0) -- the title then
 * begins the game exactly as if that key had been pressed there. 0 means it
 * ran its course. Only title_screen calls it: see attract.c for why the demo
 * may borrow the game's state there and nowhere else. */
uint16_t attract_demo(void) __banked;

#endif /* ATTRACT_H */
