/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* farlook.c - the map cursor: ';' (farlook), and the spot a ring of teleport
 * control asks for (hero_teleport, nexthack.c).
 *
 * WHY IT IS ITS OWN FILE. It was nexthack.c's until 2026-10-25, when three
 * small features in a day took the 128K's BANK_3 from 283 to 132 B free.
 * nexthack.c sits in the INDIVISIBLE nexthack+classes+spells colocate group
 * (classes.c and spells.c hand it pointers into their consts), and that unit
 * already fills its bank alone, so no packing can help it -- the third such
 * split, after nexthack_lvl.c and save.c.
 *
 * WHY THIS CUT. It is cold -- it runs only while the player moves a cursor --
 * and nothing it touches is a pointer into nexthack.c's bank: its literals go
 * only to the resident msg/msg2; mon_name() is the resident monster.c's;
 * floor_item_desc_at() hands back obj_desc's buffer, which is resident BSS;
 * grave_say() prints for itself. align_name is the one helper it shared, and
 * it keeps its own three-word copy rather than take a pointer across. What it
 * reads of the renderer -- the 128K's vx_origin and the VIEW_SHADOW in Bank 5
 * -- is resident DATA either way. hero_teleport stays in nexthack.c, the one
 * door every teleport goes through, and reaches the cursor by a __banked
 * call. */

#include "game.h"
#include "platform.h"
#include "level.h"
#include "monster.h"
#include "item.h"
#include "nexthack.h"

#ifndef __ZXNEXT
/* nexthack.c's renderer state: what is on screen, cell for cell, and the
 * first map column the 32-column viewport shows */
#define VIEW_SHADOW ((uint8_t *)0x6000u)
extern uint8_t vx_origin;
#endif

/* nexthack.c's helper, copied: its literals live in that bank, so a pointer to
 * one would read garbage here once the trampoline had swapped it out */
static const char *align_name(uint8_t a)
{
    return a == 0 ? "Lawful" : a == 1 ? "Neutral" : "Chaotic";
}

/* ---- farlook (';') ----
 * Move a cursor around the map and be told what lies there, as in NetHack.
 * Costs no turn. */

/* Say what occupies cell (x,y), honouring what the hero can actually know:
 * unexplored cells stay unknown, monsters show only in sight (or by
 * telepathy), remembered terrain reads from the map like the renderer does. */
static void look_at(uint8_t x, uint8_t y)
{
    int mi;
    char c;
    if (x == (uint8_t)hero_x && y == (uint8_t)hero_y) {
        msg("That is you.");
        return;
    }
    if (!fov_seen(x, y)) {
        msg("You have not seen that spot.");
        return;
    }
    mi = monster_at(x, y);
    if (mi >= 0 && (fov_visible(x, y) || mon_sensed())) {
        if (m_type[mi] == 'x') { msg("A potion."); return; }  /* the mimic's bait */
        if (mi == pet_idx) { msg("Your faithful dog."); return; }
        if (m_type[mi] == MON_KEEPER) { msg("The shopkeeper."); return; }
        {
            const char *nm = mon_name(m_type[mi]);
            const char *art;
            if      (m_peace[mi]) art = "A peaceful ";
            else if (m_sleep[mi]) art = "A sleeping ";
            else art = (nm[0] == 'a' || nm[0] == 'e' || nm[0] == 'i' ||
                        nm[0] == 'o' || nm[0] == 'u') ? "An " : "A ";
            msg2(art, nm, ".");
        }
        return;
    }
    c = lvl[y][x];
    switch (c) {
    case ')': case '[': case '!': case '%': case '?': case '=': case '/':
    case '&': case '(':
        msg(floor_item_desc_at(x, y));
        break;
    case '.': msg("The floor.");            break;
    case '#': msg("A corridor.");           break;
    case '|': case '-': msg("A wall.");     break;
    case '+': msg("A door.");               break;
    case CH_DOORWAY: msg("A doorway.");     break;
    case ' ': msg("Solid stone.");          break;
    case '<': msg("A staircase up.");       break;
    case '>': msg("A staircase down.");     break;
    case '^': msg("A trap.");               break;
    case '_': msg2("A ", align_name(altar_align(x, y)), " altar."); break;
    case '{': msg("A fountain.");           break;
    case CH_SINK:   msg("A sink.");         break;
    case CH_THRONE: msg("A throne.");       break;
    case CH_GRAVE:  grave_say(x, y);        break;
    case 'v': msg("A hole down to the mines."); break;
    case '$': msg("A pile of gold.");       break;
    case '*': msg("A luckstone.");          break;
    case '"': msg(dlvl == DLVL_AMULET ? "The Amulet of Yendor."
                                     : "An amulet."); break;
    default:  msg("");                      break;
    }
}

/* The farlook cursor, shared with teleport control. It is a yellow 'X' drawn
 * OVER the screen cell, the original cell restored on every step -- no
 * draw_map pass runs while it moves. The Next saves and rewrites the two
 * tilemap bytes in place; the 128K re-blits the cell from VIEW_SHADOW (which
 * always mirrors the screen) and keeps the cursor inside the current viewport
 * (it never scrolls, so a controlled teleport there reaches what is on
 * screen). Enter, '.' or ',' picks the cell and returns 1; any other key
 * gives up with 0. `look` narrates each step -- that is farlook. */
uint8_t cursor_pick(uint8_t look, uint8_t *px, uint8_t *py) __banked
{
    uint8_t cx = (uint8_t)hero_x, cy = (uint8_t)hero_y;
    uint8_t x0, x1;
    int k;
#ifdef __ZXNEXT
    uint8_t *cp;
    uint8_t s0, s1;
    x0 = 0; x1 = MAPW - 1;
#else
    uint8_t *shad = VIEW_SHADOW;
    uint8_t sc;
    uint16_t si;
    x0 = vx_origin; x1 = (uint8_t)(vx_origin + TM_W - 1);
#endif
    in_wait_nokey();
    key_rpt_slow(1);         /* the cursor glides gentler than the walk (Next) */
    for (;;) {
#ifdef __ZXNEXT
        cp = tm_cell_ptr((uint8_t)(OX + cx), (uint8_t)(OY + cy));
        s0 = cp[0]; s1 = cp[1];
        putcell((uint8_t)(OX + cx), (uint8_t)(OY + cy), 'X', C_YELLOW | C_BRIGHT);
        k = getkey_rpt();
        cp[0] = s0; cp[1] = s1;
#else
        sc = (uint8_t)(cx - vx_origin);
        si = (uint16_t)(((uint16_t)cy * TM_W + sc) << 1);
        putcell(sc, (uint8_t)(OY + cy), 'X', C_YELLOW | C_BRIGHT);
        k = getkey_rpt();
        puttile_attr(sc, (uint8_t)(OY + cy), shad[si], shad[si + 1]);
#endif
        switch (k) {
        case 'h': case  8: if (cx > x0) cx--; break;
        case 'l': case  9: if (cx < x1) cx++; break;
        case 'j': case 10: if (cy < MAPH - 1) cy++; break;
        case 'k': case 11: if (cy > 0) cy--; break;
        case 'y': if (cx > x0) cx--; if (cy > 0) cy--; break;
        case 'u': if (cx < x1) cx++; if (cy > 0) cy--; break;
        case 'b': if (cx > x0) cx--; if (cy < MAPH - 1) cy++; break;
        case 'n': if (cx < x1) cx++; if (cy < MAPH - 1) cy++; break;
        default:                       /* any other key ends it */
            key_rpt_slow(0);
            in_wait_nokey();
            *px = cx; *py = cy;
            return (uint8_t)(k == 13 || k == '.' || k == ',');
        }
        if (look) look_at(cx, cy);
    }
}

void do_farlook(void) __banked
{
    uint8_t x, y;
    msg("Look where? (moves; Enter ends)");
    cursor_pick(1, &x, &y);
    msg("");
}
