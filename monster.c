/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* monster.c - RESIDENT half of the monster module: the parallel monster arrays
 * and the small per-monster lookups the renderer calls every cell (monster_at,
 * mon_find/mon_name/mon_tile) plus pick_mon. The chase AI, combat, spawning and
 * persistence are in the BANKED monster_ai.c; keeping these lookups resident
 * means draw_map's per-cell monster_at() and the AI's calls stay direct. */

#include "monster.h"
#include "platform.h"     /* T_* tile numbers (catalogue)                     */
#include "rng.h"          /* rn2 (pick_mon)                                   */
#include "game.h"         /* dlvl (pick_mon)                                  */

/* Monster state, shared with the banked half (monster_ai.c) via the externs in
 * monster.h. DATA is resident regardless of code banking. */
uint8_t m_x[MAXMON], m_y[MAXMON], m_alive[MAXMON];
uint8_t m_hp[MAXMON];
char    m_type[MAXMON];
uint8_t mcount;

/* ---- monster catalogue (resident; mon_find/pick_mon read it) ---- */
static const MonType montypes[] = {
    /* ch  hp dmg xp mindep tile        corr name */
    { 'r',  3, 3, 1, 1, T_RAT,      0, "rat"       },
    { 'B',  3, 2, 1, 1, T_BAT,      0, "bat"       },
    { 'a',  5, 2, 3, 2, T_ACIDBLOB, 1, "acid blob" },
    { 'k',  4, 2, 2, 1, T_KOBOLD,   0, "kobold"    },
    { 'd',  6, 3, 2, 2, T_DOG,      0, "dog"       },
    { 'S',  6, 4, 3, 3, T_SNAKE,    0, "snake"     },
    { 'o',  8, 4, 4, 4, T_ORC,      0, "orc"       },
    { 'Z', 14, 4, 6, 6, T_ZOMBIE,   0, "zombie"    }
};
#define NMON ((uint8_t)(sizeof(montypes) / sizeof(montypes[0])))

const MonType *mon_find(char ch)
{
    uint8_t i;
    for (i = 0; i < NMON; i++)
        if (montypes[i].ch == ch)
            return &montypes[i];
    return &montypes[0];
}

const char *mon_name(char t) { return mon_find(t)->name; }
uint8_t     mon_tile(char t) { return mon_find(t)->tile; }

int monster_at(int x, int y)
{
    uint8_t i;
    for (i = 0; i < mcount; i++)
        if (m_alive[i] && m_x[i] == x && m_y[i] == y)
            return i;
    return -1;
}

/* pick a monster type allowed at the current depth */
char pick_mon(void)
{
    char pool[NMON];
    uint8_t n = 0, i;
    for (i = 0; i < NMON; i++)
        if (montypes[i].mindepth <= dlvl)
            pool[n++] = montypes[i].ch;
    return n ? pool[rn2(n)] : 'r';
}
