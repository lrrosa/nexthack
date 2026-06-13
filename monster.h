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

#define MAXMON 8

extern uint8_t mcount;
extern uint8_t m_x[], m_y[], m_alive[], m_hp[];
extern char    m_type[];

/* ---- monster catalogue (the table lives in monster.c, resident) ---- */
typedef struct {
    char        ch;
    uint8_t     hp;
    uint8_t     dmg;        /* bite damage is 1..dmg          */
    uint8_t     xp;         /* experience granted for a kill  */
    uint8_t     mindepth;   /* shallowest depth it appears at */
    uint8_t     tile;
    uint8_t     corr;       /* corrodes the hero's gear on contact */
    const char *name;
} MonType;

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
void attack_monster(uint8_t mi)     __banked;  /* hero hits monster mi */
void monsters_turn(void)            __banked;  /* every monster chases + attacks */
void maybe_spawn_wanderer(void)     __banked;  /* small per-turn spawn chance */

#endif /* MONSTER_H */
