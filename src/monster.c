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
#include "level.h"        /* eff_depth (mine levels play shallow)             */

/* Monster state, shared with the banked half (monster_ai.c) via the externs in
 * monster.h. DATA is resident regardless of code banking. */
uint8_t m_x[MAXMON], m_y[MAXMON], m_alive[MAXMON];
uint8_t m_hp[MAXMON];
char    m_type[MAXMON];
uint8_t m_sleep[MAXMON];  /* >0: asleep. 255 = until disturbed (spawn sleepers);
                           * smaller values tick down (wand/spell of sleep). */
uint8_t m_peace[MAXMON];  /* 1 = peaceful (Minetown natives): minds its own
                           * business until the hero draws blood (monster_ai
                           * then angers the whole town). */
uint8_t m_face[MAXMON];   /* 1 = last stepped right: directional art (dog/rat)
                           * draws mirrored. Transient like a pose -- zeroed
                           * each spawn, never saved. */
uint8_t mcount;
int8_t  pet_idx = -1;     /* the pet's slot this level (see monster.h), -1 = none */
uint8_t mon_geno[8];            /* genocided types, by char (see monster.h) */
uint8_t mon_dead[MAXLVL + 1];   /* bit i: monster i killed. Written by combat
                                 * (monster_ai.c), applied/saved by
                                 * monster_spawn.c; defined here because the
                                 * two live in different banks. */
uint8_t m_track;                /* which slots are the level's own (monster.h) */
uint8_t m_blind[MAXMON];        /* the camera's flash (monster.h) */
uint8_t m_mv[MAXMON];           /* movement points (monster.h) */
uint16_t m_haste;               /* hasted slots (monster.h) */

/* ---- monster catalogue (resident; mon_find/pick_mon read it) ---- */
static const MonType montypes[] = {
    /* ch  hp dmg xp mindep tile          corr atk       spd  name
     * spd (1.9): monster_ai monsters_turn acts once per NORMAL_SPEED banked.
     * NetHack's own speeds made the game far easier -- balance.py had the
     * Valkyrie winning 96% and the Tourist 79% against 58 and 19: the pet
     * 'd' at 16 alone was worth +29 points, and the slow monsters (a dragon
     * at 9, zombies at 6, blobs and mimics at 3) die before they hurt. So the
     * 'd' runs like the jackal it is sized as (12, and the pet with it), the
     * dragon keeps the apex's 12, the vampire is a vampire lord's 14, the
     * kobold and the orc keep 12, and the slow ones are slow by half as much:
     * zombie 8, gnome and dwarf 9, blob and mimic 6. The bat (22), snake and
     * leprechaun (15), yellow light (13) and shopkeeper (18) are NetHack's.
     * Measured (dog on, 5 seeds x 600): Valkyrie 62.5%, Wizard 44.5%, Rogue
     * 34.3%, Tourist 23.2% -- 58.2/42.7/35.8/19.3 before speed. */
    { 'r',  3, 3, 1, 1, T_RAT,         0, ATK_NONE, 12,   "rat"          },
    { 'B',  3, 2, 1, 1, T_BAT,         0, ATK_NONE, 22,   "bat"          },
    { 'a',  5, 2, 3, 2, T_ACIDBLOB,    1, ATK_NONE,  6,   "acid blob"    },
    { 'k',  4, 2, 2, 1, T_KOBOLD,      0, ATK_NONE, 12,   "kobold"       },
    { 'd',  6, 3, 2, 2, T_DOG,         0, ATK_NONE, 12,   "dog"          },
    { 'S',  6, 4, 3, 3, T_SNAKE,       0, ATK_POISON, 15, "snake"        },
    { 'o',  8, 4, 4, 4, T_ORC,         0, ATK_NONE, 12,   "orc"          },
    { 'Z', 14, 4, 6, 6, T_ZOMBIE,      0, ATK_NONE,  8,   "zombie"       },
    { 'l',  5, 2, 3, 3, T_LEPRECHAUN,  0, ATK_STEAL, 15,  "leprechaun"   },
    { 'y',  6, 3, 4, 5, T_YELLOWLIGHT, 0, ATK_BLIND, 13,  "yellow light" },
    { 'i',  9, 4, 5, 6, T_HOMUNCULUS,  0, ATK_SLEEP, 12,  "homunculus"   },
    { 'W', 12, 5, 7, 8, T_WRAITH,      0, ATK_DRAIN, 12,  "wraith"       },
    /* the floating eye neither chases nor bites (monster_ai skips its turn)
     * -- but strike it with your eyes open and its gaze freezes you. Its
     * corpse grants telepathy (item.c eat_corpse): the NetHack classic. */
    { 'e',  8, 1, 4, 5, T_FEYE,        0, ATK_NONE,  1,   "floating eye" },
    /* the deep roster: real threats for Dlvl 12+, where the shallow types
     * only recycled with inflated HP. The troll knits its wounds shut every
     * turn (monsters_turn); the vampire drains like its lesser kin the
     * wraith; the dragon is the apex -- its numbers ARE its identity. */
    { 'T', 18, 6, 10, 12, T_TROLL,   0, ATK_NONE, 12,  "troll"   },
    { 'V', 20, 6, 12, 16, T_VAMPIRE, 0, ATK_DRAIN, 14, "vampire" },
    { 'D', 28, 8, 20, 22, T_DRAGON,  0, ATK_NONE, 12,  "dragon"  },
    /* the mimic spawns as 'x' -- catalogued with the POTION tile, so every
     * renderer draws the bait for free. Any damage or the hero stepping
     * adjacent flips m_type to 'm' (monster_ai), the revealed form. Same
     * name in both rows, so kill/hit messages never spoil nor lie. */
    { 'x', 14, 5, 8, 6,   T_POTION, 0, ATK_NONE,  6,  "mimic"   },
    { 'm', 14, 5, 8, 255, T_MIMIC,  0, ATK_NONE,  6,  "mimic"   },
    /* the Amulet's keeper: posted on the Amulet cell of DLVL_AMULET by
     * spawn_level_monsters (slot 0, kill remembered by mon_dead bit 0).
     * mindepth 255 keeps it out of every random pool. */
    { 'M', 45, 9, 40, 255, T_PRIEST, 0, ATK_NONE, 12, "high priest" },
    /* the Gnomish Mines natives: never in the depth pool (mindepth 255) --
     * pick_mon injects them directly on mine levels. */
    { 'G',  4, 2, 2, 255, T_GNOME, 0, ATK_NONE,  9, "gnome" },
    { 'h',  8, 4, 5, 255, T_DWARF, 0, ATK_NONE,  9, "dwarf" },
    /* the nymph: a soft bite, but it lifts an item from your pack and she
     * blinks away across the level with it (item.c steal_item). Kill her
     * and she drops the loot; leave the level and she abandons it. */
    { 'n',  9, 2, 7, 7, T_NYMPH, 0, ATK_ITEM, 12, "nymph" },
    /* the shopkeeper: drawn as '@' (reuses T_HERO), placed only in shops, never
     * randomly spawned (pick_mon skips it), and stationary (monster_ai mon_step). */
    { MON_KEEPER, 30, 0, 0, 1, T_KEEPER, 0, ATK_NONE, 18, "shopkeeper" }
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

/* turn monster i toward the column it is about to step to (call BEFORE the
 * m_x write); a vertical step keeps the last facing, like a real animal */
void mon_face_to(uint8_t i, uint8_t nx)
{
    if (nx != m_x[i]) m_face[i] = (uint8_t)(nx > m_x[i]);
}

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
    /* the ascent is a gauntlet: with the Amulet in your pack the dungeon
     * sends its deep servants after you no matter how near the surface.
     * (eff_depth: mine levels play shallow despite their 51+ ids.) */
    uint16_t d = has_amulet ? (uint16_t)(eff_depth() + 15) : eff_depth();
    /* the mines belong to the small folk: most spawns are natives */
    if (IN_MINES(dlvl) && rn2(3) != 0)
        return rn2(2) ? 'G' : 'h';
    for (i = 0; i < NMON; i++)
        if (montypes[i].ch != MON_KEEPER && montypes[i].mindepth <= d)
            pool[n++] = montypes[i].ch;
    return n ? pool[rn2(n)] : 'r';
}
