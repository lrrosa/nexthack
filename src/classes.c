/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* classes.c - the class picker (the NetHack-identity batch).
 *
 * Fully banked INCLUDING its consts and string literals (#pragma constseg):
 * the class table and the picker screen's text cost zero resident bytes.
 * It deliberately shares nexthack.c's bank, so draw_status and show_help
 * (same bank, mapped when they run) can read class_name()'s pointer in
 * place -- a cross-bank caller could not. */

#include "game.h"
#include "platform.h"
#include "item.h"
#include "classes.h"

#ifdef __ZXNEXT
#pragma codeseg  PAGE_22_CODE
#pragma constseg PAGE_22_CODE
#else
#pragma codeseg  BANK_3
#pragma constseg BANK_3
#endif

typedef struct {
    const char *name;
    uint8_t at[6];          /* St Dx Co In Wi Ch */
    uint8_t hp, pw;
    uint8_t kit[3];         /* otyp | 0x80 = start equipped; 0xFF = empty */
    uint8_t gold;
} class_t;

/* otyps from item.c's catalogue enum (kept in sync by hand -- item.c owns it) */
#define K_DAGGER  0
#define K_SHORTSW 1
#define K_LONGSW  3
#define K_LEATHER 4
#define K_RINGML  5
#define K_HEAL    8
#define K_FOOD    17      /* +1: O_IDENTIFY joined the scroll block */
#define K_WSTRIKE 19
#define K_BFORCE  25      /* spellbook of force bolt (after O_CORPSE 24) */
#define K_EQ      0x80
#define K_NONE    0xFF

/* Four ways into the dungeon. The Valkyrie fights, the Wizard zaps, the
 * Rogue dodges, the Tourist haggles (and eats well). HP/Pw and the sheet
 * follow NetHack's flavour scaled to our numbers. */
static const class_t classes[NCLASS] = {
    /* name        St Dx Co In Wi Ch   hp pw  kit                                        $ */
    { "Valkyrie", {17,12,16, 8,10, 8}, 16, 1, {K_LONGSW|K_EQ,  K_RINGML|K_EQ,  K_NONE},   0 },
    { "Wizard",   { 8,11,10,16,14,10}, 10, 6, {K_DAGGER |K_EQ, K_BFORCE,       K_HEAL},   0 },
    { "Rogue",    {11,16,12,10, 8, 8}, 12, 2, {K_DAGGER |K_EQ, K_LEATHER|K_EQ, K_NONE},  60 },
    { "Tourist",  {10,11,12,10, 8,14}, 12, 2, {K_FOOD,         K_FOOD,         K_HEAL}, 120 },
};

/* Ask who the player is (a full-screen menu at new-game time) and fill the
 * character sheet from the choice. The kit is handed out separately
 * (give_kit) AFTER item_reset has cleared the pack. */
void pick_class(void) __banked
{
    uint8_t i;
    int k;

    tm_cls();
    print_str(4, 2, "Who are you?", C_WHITE | C_BRIGHT);
    for (i = 0; i < NCLASS; i++) {
        uint8_t y = (uint8_t)(5 + i * 2);
        putcell(4, y, (uint8_t)('a' + i), C_YELLOW | C_BRIGHT);
        putcell(5, y, ')', C_YELLOW | C_BRIGHT);
        print_str(7, y, classes[i].name, C_CYAN | C_BRIGHT);
    }
    print_str(4, 14, "Press a-d", C_WHITE | C_BRIGHT);
    in_wait_nokey();
    do { k = getkey(); } while (k < 'a' || k >= 'a' + NCLASS);
    in_wait_nokey();

    pclass = (uint8_t)(k - 'a');
    at_str = classes[pclass].at[0];
    at_dex = classes[pclass].at[1];
    at_con = classes[pclass].at[2];
    at_int = classes[pclass].at[3];
    at_wis = classes[pclass].at[4];
    at_cha = classes[pclass].at[5];
    php = pmaxhp = classes[pclass].hp;
    pw  = pmaxpw = classes[pclass].pw;
    map_dirty = 1;                  /* the menu drew over everything */
}

/* Hand out the class's starting gear + purse (call right after item_reset). */
void give_kit(void) __banked
{
    const class_t *c = &classes[pclass];
    uint8_t i;
    for (i = 0; i < 3; i++)
        if (c->kit[i] != K_NONE)
            give_item((uint8_t)(c->kit[i] & 0x7F),
                      (uint8_t)(c->kit[i] >> 7));
    gold = c->gold;
}

/* The class's display name. Only same-bank callers (nexthack.c's status/help)
 * may hold the returned pointer -- it lives in this bank. */
const char *class_name(void) __banked
{
    return classes[pclass].name;
}
