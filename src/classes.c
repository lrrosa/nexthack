/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* classes.c - the class picker (the NetHack-identity batch).
 *
 * Fully banked INCLUDING its consts and string literals (banks.json gives it
 * a "const" bank): the class table and the picker screen's text cost zero
 * resident bytes.
 * It deliberately shares nexthack.c's bank, so draw_status and show_help
 * (same bank, mapped when they run) can read class_name()'s pointer in
 * place -- a cross-bank caller could not. */

#include "game.h"
#include "platform.h"
#include "item.h"
#include "classes.h"

typedef struct {
    const char *name;
    uint8_t at[6];          /* St Dx Co In Wi Ch */
    uint8_t hp, pw;
    uint8_t kit[4];         /* otyp | 0x80 = start equipped; 0xFF = empty */
    uint8_t gold;
    uint8_t align;          /* 0 Lawful / 1 Neutral / 2 Chaotic */
    uint8_t intr;           /* INTR_* the class is born with (1.6) */
} class_t;

/* otyps from item.c's catalogue enum (kept in sync by hand -- item.c owns it) */
#define K_DAGGER  0
#define K_SHORTSW 1
#define K_MACE    2
#define K_LONGSW  3
#define K_LEATHER 4
#define K_RINGML  5
#define K_HEAL    8
#define K_FOOD    17      /* +1: O_IDENTIFY joined the scroll block */
#define K_WSTRIKE 19
#define K_WSLEEP  21
#define K_BFORCE  25      /* spellbook of force bolt (after O_CORPSE 24) */
#define K_BHEAL   26      /* spellbook of healing                        */
#define K_SPLINT  36      /* splint mail, the 1.2 ladder's first rung    */
#define K_CAMERA  80      /* the expensive camera (1.6, the Tourist's)    */
#define K_TWOHSW  81      /* the two-handed sword (the Barbarian's, 1.9)  */
#define K_EQ      0x80
#define K_NONE    0xFF

/* Eight ways into the dungeon. The Valkyrie fights, the Wizard zaps, the
 * Rogue dodges, the Tourist haggles (and eats well). HP/Pw and the sheet
 * follow NetHack's flavour scaled to our numbers.
 *
 * The 1.9 four, appended so no saved pclass shifts, each with NetHack's own
 * signature: the Samurai is born Fast (his katana is our long sword, his
 * splint mail NetHack's), the Barbarian swings a two-handed sword and shrugs
 * off poison, the Priest sees whether anything is blessed or cursed
 * (item.c: obj_desc, inv_add), and the Healer carries the healing
 * spell, a potion, a wand of sleep, a scalpel and a full purse.
 *
 * Their sheets are balance.py's, against NetHack's order (the Healer down
 * with the Tourist): the healing book is what carries a Priest or a Healer,
 * so Wisdom 14 -- the Pw-regen threshold, upkeep() -- is worth a great deal,
 * and their bodies pay for it; Dexterity counts in steps of two (to-hit
 * reads Dx >> 1). Fast is worth to the Samurai what St 17 is to the
 * Valkyrie. */
static const class_t classes[NCLASS] = {
    /* name        St Dx Co In Wi Ch   hp pw  kit                                                  $   align intr */
    { "Valkyrie", {17,12,16, 8,10, 8}, 16, 1, {K_LONGSW|K_EQ,  K_RINGML|K_EQ,  K_NONE, K_NONE},     0, 0, 0 },
    { "Wizard",   { 8,11,10,16,14,10}, 10, 6, {K_DAGGER |K_EQ, K_BFORCE,       K_HEAL, K_NONE},     0, 1, 0 },
    { "Rogue",    {11,16,12,10, 8, 8}, 12, 2, {K_DAGGER |K_EQ, K_LEATHER|K_EQ, K_NONE, K_NONE},    60, 2, INTR_STEALTH },
    { "Tourist",  {10,11,12,10, 8,14}, 12, 2, {K_FOOD,         K_FOOD,         K_HEAL, K_CAMERA}, 120, 1, 0 },
    { "Samurai",  {16,16,15, 8,10, 8}, 14, 1, {K_LONGSW|K_EQ,  K_SPLINT|K_EQ,  K_NONE, K_NONE},     0, 0, INTR_FAST },
    { "Barbarian",{16,12,16, 7, 8, 7}, 16, 1, {K_TWOHSW|K_EQ,  K_RINGML|K_EQ,  K_NONE, K_NONE},     0, 2, INTR_POISON_RES },
    { "Priest",   {12, 8,13,10,14,12}, 11, 4, {K_MACE  |K_EQ,  K_LEATHER|K_EQ, K_BHEAL, K_HEAL},    0, 1, 0 },
    { "Healer",   { 8,11,12,12,13,13}, 10, 4, {K_DAGGER|K_EQ,  K_HEAL,         K_BHEAL, K_WSLEEP}, 200, 1, INTR_POISON_RES },
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
    print_str(4, 21, "Press a-h", C_WHITE | C_BRIGHT);
    in_wait_nokey();
    do { k = getkey(); } while (k < 'a' || k >= 'a' + NCLASS);
    in_wait_nokey();

    class_apply((uint8_t)(k - 'a'));
    map_dirty = 1;                  /* the menu drew over everything */
}

/* Fill the character sheet from class k. Split out of pick_class so the
 * title's attract demo (attract.c) can field a hero of any class -- sheet,
 * then give_kit -- without the menu. */
void class_apply(uint8_t k) __banked
{
    const class_t *c = &classes[k];
    pclass = k;
    at_peak[0] = at_str = c->at[0];     /* a new hero is at every peak */
    at_peak[1] = at_dex = c->at[1];
    at_peak[2] = at_con = c->at[2];
    at_peak[3] = at_int = c->at[3];
    at_peak[4] = at_wis = c->at[4];
    at_peak[5] = at_cha = c->at[5];
    php = pmaxhp = c->hp;
    pw  = pmaxpw = c->pw;
    alignment = c->align;
    intrinsics |= c->intr;          /* new_game zeroed them just before */
}

/* Hand out the class's starting gear + purse (call right after item_reset). */
void give_kit(void) __banked
{
    const class_t *c = &classes[pclass];
    uint8_t i;
    for (i = 0; i < 4; i++)
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
