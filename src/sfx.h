/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* sfx.h - simple beeper sound effects (named, game-level wrappers). */
#ifndef SFX_H
#define SFX_H

/* All banked (PAGE_20_CODE): sound is cold (event-driven), so the trampoline
 * cost is irrelevant. The BEEPFX_* effect tables are resident library rodata. */
void sfx_hit(void)     __banked;  /* you hit a monster        */
void sfx_kill(void)    __banked;  /* a monster dies           */
void sfx_hurt(void)    __banked;  /* a monster hits you       */
void sfx_pick(void)    __banked;  /* pick up an item          */
void sfx_gold(void)    __banked;  /* pick up gold (coins)     */
void sfx_quaff(void)   __banked;  /* drink a potion           */
void sfx_eat(void)     __banked;  /* eat food                 */
void sfx_magic(void)   __banked;  /* read scroll / put on ring */
void sfx_stairs(void)  __banked;  /* use stairs               */
void sfx_levelup(void) __banked;  /* gain an experience level */
void sfx_die(void)     __banked;  /* the hero dies            */

#endif /* SFX_H */
