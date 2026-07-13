/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* game.h - shared game/player state used across modules */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* the deepest level; the Amulet of Yendor waits here (carry it back to win) */
#define DLVL_AMULET 50

/* ---- the Gnomish Mines (v0.10): a 4-level side branch ----
 * Entered through a mine entrance ('v') always found on MINES_ENTR_DLVL; its
 * levels use internal dlvl ids MINES_BASE..MINES_BASE+MINES_DEPTH-1 (51..54),
 * so every per-dlvl system (seeds, persistence masks, fog pool) just works.
 * Difficulty and loot use eff_depth() (level.h), not the raw 51+ id. The
 * luckstone waits at the bottom. */
#define MINES_ENTR_DLVL 2
#define MINES_BASE      (DLVL_AMULET + 1)
#define MINES_DEPTH     4
#define IN_MINES(d)     ((d) >= MINES_BASE)

/* deepest level id for which per-level mutations (gold/monsters/fog-of-war)
 * are remembered: the main shaft plus the mines branch. */
#define MAXLVL (MINES_BASE + MINES_DEPTH - 1)

/* player and run state (defined in nexthack.c) */
extern int      hero_x, hero_y;
extern uint16_t dlvl;
extern uint16_t turns;
extern uint8_t  php, pmaxhp;
extern uint16_t gold;
extern uint8_t  dead;
extern uint8_t  has_amulet; /* carrying the Amulet of Yendor                 */
extern uint8_t  luckstone_taken; /* the mines-bottom luckstone was claimed
                                  * (never re-placed by gen; saved)          */
extern uint8_t  won;        /* surfaced with the Amulet (victory)            */
extern uint8_t  acted;     /* did the player's action consume a turn? */
extern uint8_t  map_dirty; /* +zx renderer: force a full map redraw (unused on Next) */
extern uint8_t  map_flush; /* +zx renderer: a cell changed at a distance -- skip the
                            * fast path once, no recenter (unused on Next) */
extern int16_t  nutrition; /* hunger: drops each turn, food refills it    */

/* equipment effects (set by item.c, used in combat and the status bar) */
extern uint8_t  weapon_dmg;   /* extra melee damage from a wielded weapon */
extern uint8_t  armor_def;    /* damage reduction from worn armor         */
extern uint8_t  ac;           /* displayed armour class                   */
extern uint8_t  regen_ring;   /* wearing the ring of regeneration: upkeep
                               * mends HP twice as fast (recomputed from
                               * the worn set by recompute_gear, not saved) */
extern uint16_t xp;           /* experience points                        */
extern uint8_t  xlvl;         /* experience level                         */

/* the character sheet (the NetHack-identity batch). Attributes have real
 * effects: St = melee damage bonus, Dx = to-hit, Co = level-up HP + regen
 * speed, Ch = shop prices (In/Wi wait for spellcasting). Defined in
 * nexthack.c; set by the class at new_game; saved. */
extern uint8_t  at_str, at_dex, at_con, at_int, at_wis, at_cha;
extern uint8_t  pclass;       /* class index (nexthack.c class table)     */
extern uint8_t  intrinsics;   /* INTR_* bit flags, learned from corpses   */
extern uint8_t  pw, pmaxpw;   /* spell power (spent by 'Z', regen in upkeep) */
extern uint8_t  known_spells; /* bit per learned spell (spells.c indexes)  */
extern uint16_t max_dlvl;     /* deepest depth reached (the score screen)  */
extern uint8_t  alignment;    /* 0 Lawful / 1 Neutral / 2 Chaotic (by class) */
extern int8_t   luck;         /* hidden fortune, -5..+5: pleased gods raise it,
                               * spurned offerings lower it; sways your to-hit
                               * and whether prayer is heard at all */
#define INTR_POISON_RES 0x01  /* poison no longer drains you              */
#define INTR_SLEEP_RES  0x02  /* sleep attacks and gas traps do nothing   */
#define INTR_TELEPATHY  0x04  /* sense monsters while blind               */

/* transient status effects (per-turn countdowns; 0 = inactive). Defined in
 * nexthack.c, ticked in upkeep(). The foundation other systems hook into --
 * potions set them now; monsters and traps will later. */
extern uint8_t  st_conf;      /* confused: each step lurches a random way */
extern uint8_t  st_blind;     /* blind: you see only your own cell        */
extern uint8_t  st_sleep;     /* asleep/paralysed: you forfeit turns      */
extern uint8_t  st_poison;    /* poisoned: HP drains each turn            */

/* Elbereth: a protective word scratched in the dust. 'E' engraves it on the
 * hero's cell (el_x/el_y) with a turn life (el_life); while the hero stands on
 * a live engraving, adjacent monsters dare not strike. Ticked down in upkeep,
 * wiped on any level change. Defined in nexthack.c. */
extern uint8_t  el_x, el_y, el_life;

/* prayer: turns until your god will hear you again (0 = ready). Ticked in
 * upkeep, set by a successful prayer, saved. */
extern uint16_t pray_timeout;

/* pet: a loyal dog that hunts monsters and follows you between levels. have_pet
 * is 1 while it lives (cleared only on its death); pet_hp carries its health
 * across levels (it is re-placed beside you on each new level, not persisted as
 * a map monster). Both defined in nexthack.c, saved. See pet_idx in monster.h
 * for the live slot. */
extern uint8_t  have_pet;
extern uint8_t  pet_hp;
extern uint8_t  hero_face;  /* 1 = facing right: the hero tile draws mirrored.
                             * Set by try_move on horizontal steps (even blocked
                             * ones -- you turn toward what you bump). Transient
                             * pose, never saved. */
/* conduct counters (v0.10, saved): 0 at death/victory = the conduct held.
 * kills = by the HERO's hand (pet kills stay pacifist); corpses = flesh
 * eaten; reads = scrolls read + books studied; prayers = prayers AND altar
 * offerings (the gods count attempts, heard or not). */
extern uint16_t cnt_kills, cnt_corpses, cnt_reads, cnt_prayers;
extern uint8_t  pet_kills;  /* lifetime kills: the dog grows at 4 and at 12
                             * (+2 bite and +6 HP/cap per size; see pet_hits
                             * and the upkeep regen cap) */

#endif /* GAME_H */
