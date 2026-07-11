/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* monster.h - monsters: spawning, chase AI and combat.
 * Monsters are kept in parallel arrays (not in the terrain buffer) so they
 * can move and carry HP independently of what is drawn.
 *
 * The module is split for code banking: monster.c is the RESIDENT half (the
 * monster arrays + the small per-monster lookups the renderer calls every
 * cell: monster_at/mon_find/mon_name/mon_tile/pick_mon). monster_ai.c is the
 * BANKED half (BFS chase, combat, spawning, persistence) -- its entry points
 * are __banked; it reaches the resident lookups by direct (resident) calls. */
#ifndef MONSTER_H
#define MONSTER_H

#include <stdint.h>

#define MAXMON 10         /* slots 0-7 random mobs (mon_dead-tracked) + 8 keeper + 9 pet */
#define MON_KEEPER '@'   /* the shopkeeper (drawn as the hero tile, stationary) */

extern uint8_t mcount;
extern uint8_t m_x[], m_y[], m_alive[], m_hp[];
extern uint8_t m_sleep[];    /* >0 = asleep: 255 sleeps until disturbed (spawn
                              * sleepers), less is a turn countdown (wand/spell) */
extern char    m_type[];
extern uint8_t mon_dead[];   /* per-depth kill bitmask (bit i: slot i slain);
                              * shared by monster_ai.c (combat sets bits) and
                              * monster_spawn.c (applies/saves it) */

/* The pet's live monster slot this level, or -1 if none is placed. Re-derived
 * every level by place_pet (the pet is never a persisted map monster), so it is
 * not saved. The pet always sits in the highest occupied slot, which the uint8_t
 * mon_dead kill-bitmask never tracks (1u<<8 == 0), so it is never marked dead. */
extern int8_t  pet_idx;

/* ---- monster catalogue (the table lives in monster.c, resident) ---- */
typedef struct {
    char        ch;
    uint8_t     hp;
    uint8_t     dmg;        /* bite damage is 1..dmg          */
    uint8_t     xp;         /* experience granted for a kill  */
    uint8_t     mindepth;   /* shallowest depth it appears at */
    uint8_t     tile;
    uint8_t     corr;       /* corrodes the hero's gear on contact */
    uint8_t     atk;        /* special on-hit attack (ATK_*)        */
    const char *name;
} MonType;

/* MonType.atk -- the special effect a successful bite may inflict */
enum { ATK_NONE, ATK_POISON, ATK_BLIND, ATK_SLEEP, ATK_STEAL, ATK_DRAIN };

/* ---- RESIDENT lookups (monster.c): hot, called per cell/monster by the
 * renderer and the banked AI; not banked so those calls stay direct ---- */
int             monster_at(int x, int y);
const MonType  *mon_find(char ch);
const char     *mon_name(char t);
uint8_t         mon_tile(char t);
char            pick_mon(void);   /* a depth-appropriate monster char */

/* ---- BANKED entry points (monster_ai.c): reached via the trampoline ---- */
void spawn_level_monsters(void)     __banked;
void apply_monster_persistence(void) __banked;
void monster_reset_persistence(void) __banked;
void monster_save(uint8_t h)        __banked;
void monster_load(uint8_t h)        __banked;
void attack_monster(uint8_t mi)     __banked;  /* hero hits monster mi (melee) */
void hit_monster(uint8_t mi, uint8_t dmg) __banked; /* apply dmg (melee or thrown) */
void level_up(void)                 __banked;  /* +1 experience level (gain potion) */
void monsters_turn(void)            __banked;  /* every monster chases + attacks */
void maybe_spawn_wanderer(void)     __banked;  /* small per-turn spawn chance */
void place_shopkeeper(uint8_t x, uint8_t y) __banked;  /* add the shop's keeper */
void place_pet(void)                __banked;  /* (re)place the pet beside the hero */

#endif /* MONSTER_H */
