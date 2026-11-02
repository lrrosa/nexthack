/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* ascend.h - the end of the game (2.0): the Astral Plane and the offering of
 * the Amulet. ascend.c is BANKED (banks.json says where), so every entry point
 * is __banked; each is called from one place in a fuller bank. */
#ifndef ASCEND_H
#define ASCEND_H

#include <stdint.h>

void astral_stamp(void) __banked;     /* gen_level: the plane's map, rooms, arrival */
void astral_populate(void) __banked;  /* spawn_level_monsters: priests + the plane's own */
void offer_amulet(void) __banked;     /* do_drop: `d` the Amulet (an altar, or refused) */

#endif
