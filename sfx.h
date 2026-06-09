/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* sfx.h - simple beeper sound effects (named, game-level wrappers). */
#ifndef SFX_H
#define SFX_H

void sfx_hit(void);      /* you hit a monster        */
void sfx_kill(void);     /* a monster dies           */
void sfx_hurt(void);     /* a monster hits you       */
void sfx_pick(void);     /* pick up an item          */
void sfx_gold(void);     /* pick up gold (coins)     */
void sfx_quaff(void);    /* drink a potion           */
void sfx_eat(void);      /* eat food                 */
void sfx_magic(void);    /* read scroll / put on ring */
void sfx_stairs(void);   /* use stairs               */
void sfx_levelup(void);  /* gain an experience level */
void sfx_die(void);      /* the hero dies            */

#endif /* SFX_H */
