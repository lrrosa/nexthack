/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* secret.c - secret doors and secret corridors (1.8), and searching for them.
 *
 * NetHack hides one door in eight behind a stretch of wall, and the odd
 * corridor square behind rock, and 's' is mostly for finding them. Here the
 * choice is a side hash per cell -- never rn2, so generation, spawning and
 * every persistence index see the level exactly as before -- applied by
 * build_level right after gen_level. Ordinary levels only, from Dlvl 2: the
 * Big Room has no doors, the hand-drawn maps are drawn as they are meant to
 * be, and the mines are hewn, not built.
 *
 * ONE RULE NetHack does not keep: a secret never cuts the way through the
 * level. Before a cell is hidden, a flood from the up stairs must still reach
 * the down stairs (the Amulet's cell on Dlvl 50 -- the same cell) and the mine
 * entrance on Dlvl 2; if it does not, the cell stays open. NetHack can wall
 * you in, and 's' gets you out at one chance in seven a turn; on a Spectrum,
 * searching every wall of a dead level for the stairs is not a game. Side
 * rooms, a vault, a branch of the corridor -- those can hide. The fuzzer's
 * walk check and the attract demo's walk to '>' both lean on this.
 *
 * A door hides only where door_hash (nexthack.c) has (h & 7) == 2, and a door
 * is LOCKED only where (h & 3) == 0, so no door is ever both: a found secret
 * door is never locked, and the locked doors' map-order index -- which
 * door_open's bits name -- never shifts when one is found.
 *
 * Found ones are remembered as dug cells are (levelfov.c dug_add): a passage
 * as a tunnel, a door with DUG_DOOR. The current level's hidden cells are
 * listed here (sec_*) for 's' to try; build_level rebuilds the list on every
 * entry, before dug_restore re-opens the found ones, so 's' checks that a
 * listed cell is still hidden before it "finds" it. */

#include "level.h"
#include "game.h"
#include "rng.h"
#include "item.h"         /* eff_luck */
#include "nexthack.h"     /* door_hash */

#define SEC_MAX 6
static uint8_t sec_n, sec_x[SEC_MAX], sec_y[SEC_MAX];

/* The flood borrows the monster BFS's scratch in Bank 5 (monster_ai.c's dist
 * and bfsq): a level is being built, so no field there is current -- every
 * reader floods afresh before it reads (compute_dist_map, dist_map_from). */
#define seen ((uint8_t *)0x7400u)
#define ring ((uint16_t *)0x7A90u)

static uint8_t open_cell(uint16_t k)
{
    return walkable(((const char *)lvl)[k]) && !seen[k];
}

/* Is the way through the level still open: '<' to '>' (and to the mine
 * entrance on its level), four ways round? Four rather than the hero's eight
 * is the strict reading -- whatever it reaches, the hero can. A full ring
 * gives up and answers no, which only means the cell stays open. */
static uint8_t way_open(void)
{
    uint16_t k, n, dn = (uint16_t)dn_y * MAPW + dn_x;
    uint16_t mn = (dlvl == MINES_ENTR_DLVL) ? (uint16_t)mn_y * MAPW + mn_x : dn;
    uint8_t head = 0, tail = 0, left = (uint8_t)(mn == dn ? 1 : 2);

    for (k = 0; k < (uint16_t)(MAPH * MAPW); k++) seen[k] = 0;
    k = (uint16_t)up_y * MAPW + up_x;
    seen[k] = 1;
    ring[tail++] = k;
    while (head != tail) {
        k = ring[head++];
        if ((k == dn || k == mn) && !--left) return 1;
        /* walkable cells never touch the map's edge on a generated level,
         * so k-1/k+1 stay on the row and k-MAPW/k+MAPW on the map */
        n = k - MAPW; if (open_cell(n)) { seen[n] = 1; if ((uint8_t)(tail + 1) == head) return 0; ring[tail++] = n; }
        n = k + MAPW; if (open_cell(n)) { seen[n] = 1; if ((uint8_t)(tail + 1) == head) return 0; ring[tail++] = n; }
        n = k - 1;    if (open_cell(n)) { seen[n] = 1; if ((uint8_t)(tail + 1) == head) return 0; ring[tail++] = n; }
        n = k + 1;    if (open_cell(n)) { seen[n] = 1; if ((uint8_t)(tail + 1) == head) return 0; ring[tail++] = n; }
    }
    return 0;
}

/* a straight stretch of corridor: '#' both ways along it, rock both sides */
static uint8_t mid_corridor(uint8_t x, uint8_t y)
{
    return (uint8_t)((lvl[y][x - 1] == '#' && lvl[y][x + 1] == '#' &&
                      lvl[y - 1][x] == ' ' && lvl[y + 1][x] == ' ') ||
                     (lvl[y - 1][x] == '#' && lvl[y + 1][x] == '#' &&
                      lvl[y][x - 1] == ' ' && lvl[y][x + 1] == ' '));
}

void secrets_hide(void) __banked
{
    uint8_t x, y;
    sec_n = 0;
    if (dlvl < 2 || IN_MINES(dlvl) || level_is_special()) return;
    for (y = 1; y < MAPH - 1; y++)
        for (x = 1; x < MAPW - 1; x++) {
            char c = lvl[y][x], hid;
            if (sec_n == SEC_MAX) return;
            if (c == '+') {
                /* one door in eight, as NetHack's dodoor -- never a shop's:
                 * its keeper must be able to leave, and a shop you have to
                 * search for is no shop */
                if ((door_hash(x, y) & 7u) != 2 || shop_in_room(x, y)) continue;
                hid = (lvl[y][x - 1] == '-' || lvl[y][x + 1] == '-') ? '-' : '|';
            } else if (c == '#') {
                /* the odd square of a straight corridor, about one in 64:
                 * the dead end that says "search here" */
                uint16_t h = (uint16_t)(world_seed * 13u + (uint16_t)dlvl * 977u
                                        + (uint16_t)x * 59u + (uint16_t)y * 211u);
                h ^= (uint16_t)(h << 7);
                h ^= (uint16_t)(h >> 9);
                if ((h & 63u) != 5 || !mid_corridor(x, y)) continue;
                hid = ' ';
            } else {
                continue;
            }
            lvl[y][x] = hid;
            if (!way_open()) { lvl[y][x] = c; continue; }
            sec_x[sec_n] = x; sec_y[sec_n] = y; sec_n++;
        }
}

/* Open listed secret i for good: the map, and the dug pool that remembers it.
 * 1 for a door, 2 for a passage, 0 if it is not hidden any more (found on an
 * earlier visit, or dug through). */
static uint8_t reveal(uint8_t i)
{
    uint8_t x = sec_x[i], y = sec_y[i];
    char *c = &lvl[y][x];
    if (*c == ' ') {
        *c = '#';
        dug_add(x, y);
        map_flush = 1;
        return 2;
    }
    if (*c == '|' || *c == '-') {
        *c = '+';
        dug_add(x, (uint8_t)(y | DUG_DOOR));
        map_flush = 1;
        return 1;
    }
    return 0;
}

/* 's': each hidden cell beside you turns up one search in five -- NetHack's
 * one in seven, a little kinder because this game has no count prefix (no
 * "20s"); luck shortens or stretches it, as NetHack's rnl does. Returns the
 * kinds found (1 door, 2 passage). */
uint8_t secrets_search(void) __banked
{
    uint8_t i, found = 0, odds;
    int8_t lk = eff_luck();
    odds = (uint8_t)(lk > 0 ? 3 : lk < 0 ? 9 : 5);
    for (i = 0; i < sec_n; i++) {
        int dx = (int)sec_x[i] - hero_x, dy = (int)sec_y[i] - hero_y;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) continue;
        if (rn2(odds)) continue;
        found |= reveal(i);
    }
    return found;
}

/* A blessed scroll of magic mapping shows the secret doors for what they are,
 * as NetHack's does; the passages stay rock. */
void secrets_reveal_doors(void) __banked
{
    uint8_t i;
    for (i = 0; i < sec_n; i++)
        if (lvl[sec_y[i]][sec_x[i]] != ' ') reveal(i);
}

/* A restored game puts the hero back on the saved square -- and a save
 * made before 1.8 knows nothing of secrets: that square may be a doorway
 * that is wall now. Open whatever is underfoot (mainentry.c, after
 * build_level). */
void secrets_underfoot(void) __banked
{
    uint8_t i;
    for (i = 0; i < sec_n; i++)
        if (sec_x[i] == (uint8_t)hero_x && sec_y[i] == (uint8_t)hero_y) reveal(i);
}
