/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* level.c - the RESIDENT leaf of the level module: the terrain buffer and the
 * tiny per-cell/per-move accessors the renderer and movement call constantly
 * (terrain/walkable/tile_for). Kept resident so banked code (draw_map etc.)
 * reaches them by direct calls with no trampoline.
 *
 * The rest of the level module is banked: procedural generation + persistence
 * in levelgen.c, field-of-view + save/restore in levelfov.c. Both read this
 * file's lvl[] (and write rcount/stairs) via the externs in level.h. */

#include "level.h"
#include "platform.h"     /* T_* tile numbers */
#include "game.h"         /* dlvl */
#include "rng.h"          /* world_seed */

char    lvl[MAPH][MAPW];
uint8_t rcount;
uint8_t up_x, up_y, dn_x, dn_y;
uint8_t mn_x, mn_y;   /* the mine entrance cell (meaningful on MINES_ENTR_DLVL) */

/* The difficulty/loot depth of the current level: mine levels carry internal
 * ids 51+ but play like shallow depths just past their entrance. Resident:
 * every spawner and the item resolver leans on it. */
uint16_t eff_depth(void)
{
    return IN_MINES(dlvl) ? (uint16_t)(MINES_ENTR_DLVL + 1 + (dlvl - MINES_BASE))
                          : dlvl;
}

/* An altar's alignment (0 Lawful / 1 Neutral / 2 Chaotic) -- a pure hash of the
 * world seed, depth and cell, so it is the same every visit and never touches
 * the RNG stream (level generation stays in sync). Resident so both banks
 * (item.c's sacrifice, nexthack.c's describe/status) call it directly. */
uint8_t altar_align(uint8_t x, uint8_t y)
{
    uint16_t h = (uint16_t)(world_seed + (uint16_t)dlvl * 0x61C9u
                            + (uint16_t)x * 13u + (uint16_t)y * 7u);
    return (uint8_t)(h % 3u);
}

char terrain(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH)
        return ' ';
    return lvl[y][x];
}

int walkable(char c)
{
    return !(c == '|' || c == '-' || c == ' ');
}

uint8_t tile_for(char c)
{
    switch (c) {
    case '.': return T_FLOOR;
    case '^': return T_TRAP;
    case '_': return T_ALTAR;
    case '#': return T_CORR;
    case '-':
    case '|': return IN_MINES(dlvl) ? T_MINEWALL : T_WALL;
    case '+': return T_DOOR;
    case '<': return T_SUP;
    case '>': return T_SDOWN;
    case '$': return T_GOLD;
    case '%': return T_FOOD;
    case ')': return T_WEAPON;
    case '[': return T_ARMOR;
    case '!': return T_POTION;
    case '?': return T_SCROLL;
    case '=': return T_RING;
    case '/': return T_WAND;
    case '&': return T_BOOK;
    case '{': return T_FOUNTAIN;
    case '"': return T_AMULET;
    case 'v': return T_MINEHOLE;
    case '*': return T_LUCKSTONE;
    default:  return T_ROCK;
    }
}
