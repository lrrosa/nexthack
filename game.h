/* game.h - shared game/player state used across modules */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* deepest level for which per-level mutations (gold/monsters) are remembered */
#define MAXLVL 24

/* player and run state (defined in nhnext.c) */
extern int      hero_x, hero_y;
extern uint16_t dlvl;
extern uint16_t turns;
extern uint8_t  php, pmaxhp;
extern uint16_t gold;
extern uint8_t  dead;
extern uint8_t  acted;     /* did the player's action consume a turn? */

/* equipment effects (set by item.c, used in combat and the status bar) */
extern uint8_t  weapon_dmg;   /* extra melee damage from a wielded weapon */
extern uint8_t  armor_def;    /* damage reduction from worn armor         */
extern uint8_t  ac;           /* displayed armour class                   */

#endif /* GAME_H */
