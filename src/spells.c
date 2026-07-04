/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* spells.c - spellbooks and casting (the arcane batch).
 *
 * Reading a spellbook ('r') LEARNS its spell (a bit in known_spells; the book
 * survives); 'Z' casts from a menu, spending Pw. Casting can fail on a weak
 * mind: rn2(20) >= In + xlvl wastes the turn and the power -- the Wizard
 * (In 16) almost never fumbles, the Valkyrie (In 8) often does.
 *
 * Banked INCLUDING consts and string literals (#pragma constseg), sharing
 * nexthack.c's bank like classes.c -- spell names and messages cost zero
 * resident bytes and are only consumed while this bank is mapped. */

#include "game.h"
#include "platform.h"
#include "level.h"
#include "monster.h"
#include "sfx.h"
#include "rng.h"
#include "spells.h"

#ifdef __ZXNEXT
#pragma codeseg  PAGE_22_CODE
#pragma constseg PAGE_22_CODE
#else
#pragma codeseg  BANK_3
#pragma constseg BANK_3
#endif

#define NSPELL 4
#define SP_FORCE 0
#define SP_HEAL  1
#define SP_SLEEP 2
#define SP_TELE  3

static const char *const sp_name[NSPELL] = {
    "force bolt", "healing", "sleep", "teleportation"
};
static const uint8_t sp_cost[NSPELL] = { 5, 5, 7, 10 };

/* 'r' on a spellbook: learn its spell (books carry the index in prop). */
void learn_spell(uint8_t idx) __banked
{
    if (idx >= NSPELL) return;
    if (known_spells & (uint8_t)(1u << idx)) {
        msg("You already know that spell.");
        return;
    }
    known_spells |= (uint8_t)(1u << idx);
    msg2("You learn ", sp_name[idx], "!");
    sfx_magic();
}

/* ask for one of the eight directions; 0 = cancelled */
static int read_dir(int *dx, int *dy)
{
    int k;
    msg("In which direction?");
    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    *dx = 0; *dy = 0;
    switch (k) {
    case 'h': case  8: *dx = -1;          break;
    case 'l': case  9: *dx =  1;          break;
    case 'j': case 10: *dy =  1;          break;
    case 'k': case 11: *dy = -1;          break;
    case 'y': *dx = -1; *dy = -1;         break;
    case 'u': *dx =  1; *dy = -1;         break;
    case 'b': *dx = -1; *dy =  1;         break;
    case 'n': *dx =  1; *dy =  1;         break;
    default:  msg("Never mind.");         return 0;
    }
    return 1;
}

#define SPELL_RANGE 8

/* a bolt that flies up to SPELL_RANGE cells and acts on the first monster */
static void spell_ray(uint8_t sp, int dx, int dy)
{
    int x = hero_x, y = hero_y;
    uint8_t r;
    int mi;
    for (r = 0; r < SPELL_RANGE; r++) {
        char c;
        x += dx; y += dy;
        if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) break;
        c = lvl[y][x];
        if (c == '|' || c == '-' || c == ' ' || c == '+') break;  /* stops */
        mi = monster_at(x, y);
        if (mi < 0) continue;
        if (sp == SP_FORCE) {
            hit_monster((uint8_t)mi, (uint8_t)(rn2(6) + 4));
        } else {                     /* SP_SLEEP */
            m_sleep[mi] = (uint8_t)(rn2(8) + 6);
            msg2("The ", mon_name(m_type[mi]), " falls asleep.");
        }
        return;
    }
    msg("The spell fizzles out.");
}

/* 'Z': pick a known spell (full-screen menu), spend Pw, cast it. A cancel or
 * an unaffordable pick costs nothing; a fumbled cast costs the turn + power. */
void do_cast(void) __banked
{
    uint8_t i, row;
    int k, dx, dy;
    uint8_t idx, x;

    if (!known_spells) { msg("You don't know any spells."); return; }

    for (i = 0; i <= 21; i++) clear_line(i, C_BLACK);
    map_dirty = 1;                       /* restore the map on return */
    print_str(0, 0, "Cast which spell?", C_WHITE | C_BRIGHT);
    row = 2;
    for (i = 0; i < NSPELL; i++) {
        if (!(known_spells & (uint8_t)(1u << i))) continue;
        putcell(1, row, (uint8_t)('a' + i), C_YELLOW | C_BRIGHT);
        putcell(2, row, ')', C_YELLOW | C_BRIGHT);
        x = print_str(4, row, sp_name[i], C_CYAN | C_BRIGHT);
        x = print_str(x, row, "  Pw:", C_WHITE);
        put_uint(x, row, sp_cost[i], C_WHITE);
        row++;
    }
    print_str(1, (uint8_t)(row + 1), "(letter, else cancel)", C_CYAN | C_BRIGHT);
    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    idx = (uint8_t)(k - 'a');
    if (idx >= NSPELL || !(known_spells & (uint8_t)(1u << idx))) {
        msg("Never mind.");
        return;
    }
    if (pw < sp_cost[idx]) { msg("You lack the power."); return; }

    dx = dy = 0;
    if ((idx == SP_FORCE || idx == SP_SLEEP) && !read_dir(&dx, &dy))
        return;                          /* cancelled at the direction: free */

    pw = (uint8_t)(pw - sp_cost[idx]);
    acted = 1; turns++;
    if (rn2(20) >= (uint8_t)(at_int + xlvl)) {   /* a weak mind fumbles */
        msg("You fail to concentrate!");
        return;
    }
    sfx_magic();
    if (idx == SP_FORCE || idx == SP_SLEEP) {
        spell_ray(idx, dx, dy);
    } else if (idx == SP_HEAL) {
        php = (uint8_t)(php + rn2(8) + 6);
        if (php > pmaxhp) php = pmaxhp;
        msg("You feel better.");
    } else {                             /* SP_TELE: whisk yourself away */
        uint8_t tx, ty;
        level_random_floor(&tx, &ty);
        hero_x = tx; hero_y = ty;
        map_dirty = 1;                   /* +zx: recenter on the new spot */
        msg("You blink across the level.");
    }
}
