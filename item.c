/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item.c - inventory and items (pragmatic v1).
 *
 * The inventory is a flat list of item type chars. Equipment is tracked with
 * simple flags; the first weapon/armor in the list is the equipped one. Item
 * effects are intentionally minimal for now:
 *   weapon -> +2 melee damage    armor -> -1 damage taken (AC 10 -> 8)
 *   potion -> heal               food  -> flavour (no hunger system yet)
 */

#include "item.h"
#include "game.h"        /* hero_x/y, php, pmaxhp, weapon_dmg, armor_def, ac */
#include "platform.h"    /* drawing, messages, getkey                        */
#include "level.h"       /* lvl, terrain                                     */
#include "rng.h"         /* rn2                                              */
#include "sfx.h"         /* sound effects                                    */

#define MAXINV 16

static char    inv_type[MAXINV];
static uint8_t inv_count;
static uint8_t wielded_flag;     /* a weapon is wielded     */
static uint8_t worn_armor;       /* armor is worn           */
static uint8_t worn_ring;        /* ring of protection worn */

/* recompute AC and damage reduction from worn gear */
static void recompute_def(void)
{
    armor_def = (uint8_t)((worn_armor ? 1 : 0) + (worn_ring ? 1 : 0));
    ac = (uint8_t)(10 - (worn_armor ? 2 : 0) - (worn_ring ? 1 : 0));
}

static const char *item_name(char t)
{
    switch (t) {
    case ')': return "a dagger";
    case '[': return "leather armor";
    case '!': return "a potion of healing";
    case '%': return "a food ration";
    case '?': return "a scroll";
    case '=': return "a ring of protection";
    case '"': return "the Amulet of Yendor";
    default:  return "something strange";
    }
}

static int find_first(char t)
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (inv_type[i] == t)
            return i;
    return -1;
}

static int inv_add(char t)
{
    if (inv_count >= MAXINV) return 0;
    inv_type[inv_count++] = t;
    return 1;
}

static void inv_remove(uint8_t s)
{
    uint8_t i;
    for (i = s; i + 1 < inv_count; i++)
        inv_type[i] = inv_type[i + 1];
    inv_count--;
}

void item_reset(void)
{
    inv_count = 0;
    wielded_flag = 0;
    worn_armor = 0;
    worn_ring = 0;
    weapon_dmg = 0;
    recompute_def();
}

void do_pickup(void)
{
    char c = terrain(hero_x, hero_y);
    if (c == '"') {                       /* the Amulet of Yendor */
        if (inv_add(c)) {
            has_amulet = 1;
            level_take_item((uint8_t)hero_x, (uint8_t)hero_y);
            msg("You feel a wondrous power!  Bring the Amulet up to the surface.");
            sfx_levelup();
        } else {
            msg("Your pack is full.");
        }
    } else if (c == ')' || c == '[' || c == '!' || c == '%' ||
               c == '?' || c == '=') {
        if (inv_add(c)) {
            level_take_item((uint8_t)hero_x, (uint8_t)hero_y);
            msg2("You pick up ", item_name(c), ".");
            sfx_pick();
        } else {
            msg("Your pack is full.");
        }
    } else {
        msg("There is nothing here to pick up.");
    }
}

void show_inventory(void)
{
    uint8_t i, y;
    uint8_t first_weapon = 1, first_armor = 1, first_ring = 1;

    for (y = 1; y <= 21; y++)        /* clear the map area */
        clear_line(y, C_BLACK);

    print_str(2, 2, "Inventory   (press any key to continue)",
              C_WHITE | C_BRIGHT);

    if (inv_count == 0) {
        print_str(4, 4, "Your pack is empty.", C_WHITE);
    } else {
        for (i = 0; i < inv_count; i++) {
            char t = inv_type[i];
            uint8_t x;
            putcell(4, (uint8_t)(4 + i), (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
            x = print_str(5, (uint8_t)(4 + i), " - ", C_WHITE);
            x = print_str(x, (uint8_t)(4 + i), item_name(t), C_WHITE | C_BRIGHT);
            if (t == ')' && wielded_flag && first_weapon) {
                print_str(x, (uint8_t)(4 + i), " (wielded)", C_CYAN | C_BRIGHT);
                first_weapon = 0;
            } else if (t == '[' && worn_armor && first_armor) {
                print_str(x, (uint8_t)(4 + i), " (worn)", C_CYAN | C_BRIGHT);
                first_armor = 0;
            } else if (t == '=' && worn_ring && first_ring) {
                print_str(x, (uint8_t)(4 + i), " (on hand)", C_CYAN | C_BRIGHT);
                first_ring = 0;
            }
        }
    }

    in_wait_nokey();    /* wait for the 'i' that opened this to be released */
    getkey();           /* then wait for a fresh key press                  */
    in_wait_nokey();
    /* the caller redraws the map afterwards */
}

void do_wield(void)
{
    if (find_first(')') < 0) {
        msg("You have no weapon to wield.");
        return;
    }
    wielded_flag = 1;
    weapon_dmg = 2;
    msg("You wield a dagger.");
}

void do_wear(void)
{
    if (find_first('[') < 0) {
        msg("You have no armor to wear.");
        return;
    }
    if (worn_armor) {
        msg("You are already wearing armor.");
        return;
    }
    worn_armor = 1;
    recompute_def();
    msg("You don your leather armor.");
}

void do_quaff(void)
{
    int s = find_first('!');
    uint8_t heal;

    if (s < 0) {
        msg("You have no potions to drink.");
        return;
    }
    heal = (uint8_t)(rn2(8) + 4);          /* 4..11 */
    php = (uint8_t)(php + heal);
    if (php > pmaxhp) php = pmaxhp;
    inv_remove((uint8_t)s);
    msg("You feel much better.");
    sfx_quaff();
}

void do_eat(void)
{
    int s = find_first('%');
    if (s < 0) {
        msg("You have nothing to eat.");
        return;
    }
    if (nutrition > 1200) {
        msg("You are too full to eat now.");
        return;
    }
    nutrition += 800;
    if (nutrition > 1500) nutrition = 1500;
    inv_remove((uint8_t)s);
    msg("You finish your meal.  Delicious!");
    sfx_eat();
}

void do_puton(void)
{
    if (find_first('=') < 0) {
        msg("You have no ring to put on.");
        return;
    }
    if (worn_ring) {
        msg("You are already wearing a ring.");
        return;
    }
    worn_ring = 1;
    recompute_def();
    msg("The ring tingles on your finger.  You feel protected.");
    sfx_magic();
}

void do_read(void)
{
    int s = find_first('?');
    if (s < 0) {
        msg("You have nothing to read.");
        return;
    }
    inv_remove((uint8_t)s);
    sfx_magic();
    if (rn2(2)) {                       /* scroll of magic mapping */
        fov_reveal();
        msg("A map of the level forms in your mind!");
    } else {                            /* scroll of teleportation */
        uint8_t tx, ty;
        level_random_floor(&tx, &ty);
        hero_x = tx; hero_y = ty;
        msg("You feel a wrenching sensation.");
    }
}
