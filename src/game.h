/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* game.h - shared game/player state used across modules */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* the deepest level; the Amulet of Yendor waits here (carry it back to win) */
#define DLVL_AMULET 50

/* deepest level for which per-level mutations (gold/monsters/fog-of-war) are
 * remembered. Must be >= DLVL_AMULET (the deepest reachable level); kept tight
 * so the fog-of-war bitmaps leave room for the esxDOS save buffers in RAM. */
#define MAXLVL DLVL_AMULET

/* player and run state (defined in nexthack.c) */
extern int      hero_x, hero_y;
extern uint16_t dlvl;
extern uint16_t turns;
extern uint8_t  php, pmaxhp;
extern uint16_t gold;
extern uint8_t  dead;
extern uint8_t  has_amulet; /* carrying the Amulet of Yendor                 */
extern uint8_t  won;        /* surfaced with the Amulet (victory)            */
extern uint8_t  acted;     /* did the player's action consume a turn? */
extern int16_t  nutrition; /* hunger: drops each turn, food refills it    */

/* equipment effects (set by item.c, used in combat and the status bar) */
extern uint8_t  weapon_dmg;   /* extra melee damage from a wielded weapon */
extern uint8_t  armor_def;    /* damage reduction from worn armor         */
extern uint8_t  ac;           /* displayed armour class                   */
extern uint16_t xp;           /* experience points                        */
extern uint8_t  xlvl;         /* experience level                         */

#endif /* GAME_H */
