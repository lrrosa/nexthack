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

char    lvl[MAPH][MAPW];
uint8_t rcount;
uint8_t up_x, up_y, dn_x, dn_y;

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
    case '|': return T_WALL;
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
    case '"': return T_AMULET;
    default:  return T_ROCK;
    }
}
