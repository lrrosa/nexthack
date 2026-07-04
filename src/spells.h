/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* spells.h - spellbooks and casting (spells.c, banked with nexthack.c). */
#ifndef SPELLS_H
#define SPELLS_H

#include <stdint.h>

void learn_spell(uint8_t idx) __banked;  /* 'r' on a spellbook (idx = prop)   */
void do_cast(void) __banked;             /* 'Z': menu, spend Pw, cast         */

#endif /* SPELLS_H */
