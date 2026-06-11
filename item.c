/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item.c - inventory, the object catalogue and item actions.
 *
 * Each inventory slot is an obj_t: a catalogue type (otyp), an enchantment
 * (+N to weapon damage / armour class), an erosion level (rust/corrosion,
 * used from Phase 18) and a "worn" flag for the equipped weapon/armour/ring.
 *
 * The floor only stores an item's CLASS char ( ) [ ! % ? = ) in the terrain
 * buffer, so generation stays untouched and deterministic. The *specific*
 * object (which weapon, what enchantment) is resolved when the hero picks it
 * up, deterministically from depth + position + world seed - so a given floor
 * item is always the same thing, without consuming the level-generation RNG.
 */

#include "item.h"
#include "game.h"        /* hero_x/y, php, pmaxhp, weapon_dmg, armor_def, ac */
#include "platform.h"    /* drawing, messages, getkey, file_*                */
#include "level.h"       /* terrain, level_take_item, level_random_floor     */
#include "rng.h"         /* rn2, world_seed                                  */
#include "sfx.h"         /* sound effects                                    */

#define MAXINV 24

/* ---- object catalogue (like montypes[]) ---- */

enum {
    O_DAGGER, O_SHORTSW, O_MACE, O_LONGSW,     /* ')' weapons */
    O_LEATHER, O_RINGMAIL, O_CHAIN, O_PLATE,   /* '[' armour  */
    O_HEAL, O_EXHEAL,                          /* '!' potions */
    O_MAPPING, O_TELEPORT,                     /* '?' scrolls */
    O_PROTECT,                                 /* '=' ring    */
    O_FOOD,                                    /* '%' food    */
    O_AMULET,                                  /* '"' amulet  */
    NUMOBJ
};

typedef struct {
    char        cls;     /* class char in the terrain buffer                  */
    uint8_t     prop;    /* weapon:+dmg  armour:AC bonus  potion:heal base    */
    uint16_t    price;   /* base shop price (used from Phase 20)              */
    uint8_t     mindep;  /* earliest depth at which it is generated           */
    const char *name;
} objtype_t;

static const objtype_t objtypes[NUMOBJ] = {
    /* cls prop price mindep name */
    { ')',  2,    5,  1, "dagger" },
    { ')',  3,   15,  2, "short sword" },
    { ')',  4,   40,  5, "mace" },
    { ')',  5,   80,  9, "long sword" },
    { '[',  2,   10,  1, "leather armor" },
    { '[',  3,   40,  3, "ring mail" },
    { '[',  4,  100,  6, "chain mail" },
    { '[',  5,  200, 10, "plate mail" },
    { '!',  7,   20,  1, "potion of healing" },
    { '!', 14,   60,  4, "potion of extra healing" },
    { '?',  0,   40,  1, "scroll of magic mapping" },
    { '?',  0,   60,  1, "scroll of teleportation" },
    { '=',  1,  150,  3, "ring of protection" },
    { '%',  0,   10,  1, "food ration" },
    { '"',  0,    0, 50, "the Amulet of Yendor" }
};

typedef struct {
    uint8_t otyp;
    int8_t  ench;    /* +N enchantment (weapon dmg / armour class)        */
    uint8_t ero;     /* erosion level (rust/corrosion); 0 for now         */
    uint8_t worn;    /* 1 if this is the equipped weapon/armour/ring      */
} obj_t;

static obj_t   inv[MAXINV];
static uint8_t inv_count;

/* ---- gear effects ---- */

/* recompute the combat globals (weapon_dmg, armor_def, ac) from worn gear.
 * Erosion subtracts from an item's bonus; enchantment adds to it. */
static void recompute_gear(void)
{
    uint8_t i, base_ac = 10, redux = 0;

    weapon_dmg = 0;
    for (i = 0; i < inv_count; i++) {
        const objtype_t *t;
        int eff;
        if (!inv[i].worn) continue;
        t = &objtypes[inv[i].otyp];
        eff = (int)t->prop + inv[i].ench - inv[i].ero;
        if (eff < 0) eff = 0;
        if (t->cls == ')') {
            weapon_dmg = (uint8_t)eff;
        } else if (t->cls == '[') {
            if ((uint8_t)eff <= base_ac) base_ac -= (uint8_t)eff;
            if (eff > 0) redux += (uint8_t)(eff > 1 ? eff - 1 : 1);
        } else if (t->cls == '=') {
            if (t->prop <= base_ac) base_ac -= t->prop;
            redux += t->prop;
        }
    }
    ac = base_ac;
    armor_def = redux;
}

/* a printable description with an erosion prefix and +N enchantment, e.g.
 * "+2 long sword" or "rusty chain mail". Returns a shared static buffer. */
static const char *obj_desc(const obj_t *o)
{
    static char buf[32];
    const objtype_t *t = &objtypes[o->otyp];
    char *p = buf;
    const char *s;

    if (o->ero) {
        s = (o->ero >= 2) ? "corroded " : "rusty ";
        while (*s) *p++ = *s++;
    }
    if (o->ench > 0 && (t->cls == ')' || t->cls == '[')) {
        *p++ = '+';
        *p++ = (char)('0' + (o->ench % 10));
        *p++ = ' ';
    }
    s = t->name;
    while (*s) *p++ = *s++;
    *p = 0;
    return buf;
}

/* ---- inventory helpers ---- */

static int find_class(char cls)
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (objtypes[inv[i].otyp].cls == cls) return i;
    return -1;
}

/* index of the strongest item of a class (highest prop + ench - erosion), so
 * 'w'/'W'/'P' equip the best you carry regardless of inventory order */
static int find_best(char cls)
{
    int best = -1, bestval = -999;
    uint8_t i;
    for (i = 0; i < inv_count; i++) {
        int v;
        if (objtypes[inv[i].otyp].cls != cls) continue;
        v = (int)objtypes[inv[i].otyp].prop + inv[i].ench - inv[i].ero;
        if (v > bestval) { bestval = v; best = i; }
    }
    return best;
}

static void unworn_class(char cls)   /* take off whatever of this class is worn */
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (objtypes[inv[i].otyp].cls == cls) inv[i].worn = 0;
}

static int inv_add(const obj_t *o)
{
    if (inv_count >= MAXINV) return 0;
    inv[inv_count++] = *o;
    return 1;
}

static void inv_remove(uint8_t s)
{
    while ((uint8_t)(s + 1) < inv_count) { inv[s] = inv[s + 1]; s++; }
    inv_count--;
}

void item_reset(void)
{
    inv_count = 0;
    weapon_dmg = 0;
    recompute_gear();
}

/* ---- picking up: resolve the concrete object deterministically ---- */

/* a pseudo-random value for the item lying at (x,y) on this depth; pure (does
 * not touch the RNG stream) so level generation/persistence stays in sync */
static uint16_t item_hash(uint8_t x, uint8_t y)
{
    uint16_t h = (uint16_t)(world_seed + (uint16_t)dlvl * 2657u
                            + (uint16_t)x * 131u + (uint16_t)y * 1009u);
    h ^= (uint16_t)(h << 7);
    h ^= (uint16_t)(h >> 9);
    h ^= (uint16_t)(h << 8);
    return h ? h : 0xA5A5u;
}

/* pick a concrete object of class cls that may appear at the current depth */
static uint8_t resolve_otyp(char cls, uint16_t h)
{
    uint8_t i, elig[NUMOBJ], n = 0;
    for (i = 0; i < NUMOBJ; i++)
        if (objtypes[i].cls == cls && objtypes[i].mindep <= (uint8_t)dlvl)
            elig[n++] = i;
    if (n == 0) {
        for (i = 0; i < NUMOBJ; i++)
            if (objtypes[i].cls == cls) return i;
        return O_FOOD;
    }
    return elig[h % n];
}

/* resolve the concrete object lying at (x,y) - shared by the "you see here"
 * look and by pickup, so they always agree */
static void resolve_floor(uint8_t x, uint8_t y, obj_t *o)
{
    char c = terrain(x, y);
    uint16_t h = item_hash(x, y);

    o->ench = 0;
    o->ero  = 0;
    o->worn = 0;
    if (c == '"') {
        o->otyp = O_AMULET;
        return;
    }
    o->otyp = resolve_otyp(c, h);
    if (c == ')' || c == '[') {              /* small, depth-scaled enchant */
        uint8_t roll = (uint8_t)((h >> 5) % 100u);
        if (roll < (uint8_t)dlvl)       o->ench = 1;
        if (roll < (uint8_t)(dlvl / 3)) o->ench = 2;
    }
}

/* description of the item on the hero's cell (for the "You see here" message) */
const char *floor_item_desc(void)
{
    obj_t o;
    resolve_floor((uint8_t)hero_x, (uint8_t)hero_y, &o);
    return obj_desc(&o);
}

void do_pickup(void)
{
    char c = terrain(hero_x, hero_y);
    obj_t o;

    if (c != '"' && c != ')' && c != '[' && c != '!' &&
        c != '%' && c != '?' && c != '=') {
        msg("There is nothing here to pick up.");
        return;
    }

    resolve_floor((uint8_t)hero_x, (uint8_t)hero_y, &o);
    if (!inv_add(&o)) { msg("Your pack is full."); return; }
    level_take_item((uint8_t)hero_x, (uint8_t)hero_y);

    if (c == '"') {
        has_amulet = 1;
        msg("You feel a wondrous power!  Bring the Amulet up to the surface.");
        sfx_levelup();
    } else {
        msg2("You pick up ", obj_desc(&o), ".");
        sfx_pick();
    }
}

void show_inventory(void)
{
    uint8_t i, y;

    for (y = 1; y <= 21; y++)        /* clear the map area */
        clear_line(y, C_BLACK);

    print_str(2, 2, "Inventory   (press any key to continue)",
              C_WHITE | C_BRIGHT);

    if (inv_count == 0) {
        print_str(4, 4, "Your pack is empty.", C_WHITE);
    } else {
        for (i = 0; i < inv_count; i++) {       /* two columns of 12 */
            char cls = objtypes[inv[i].otyp].cls;
            uint8_t row = (uint8_t)(4 + (i % 12));
            uint8_t cx  = (uint8_t)(i < 12 ? 2 : 42);
            uint8_t x;
            putcell(cx, row, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
            x = print_str((uint8_t)(cx + 1), row, " - ", C_WHITE);
            x = print_str(x, row, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
            if (inv[i].worn) {
                const char *w = (cls == ')') ? " (wielded)" :
                                (cls == '[') ? " (worn)"    :
                                (cls == '=') ? " (on hand)" : "";
                print_str(x, row, w, C_CYAN | C_BRIGHT);
            }
        }
    }

    in_wait_nokey();    /* wait for the 'i' that opened this to be released */
    getkey();           /* then wait for a fresh key press                  */
    in_wait_nokey();
    /* the caller redraws the map afterwards */
}

/* ---- equip / use ---- */

void do_wield(void)
{
    int s = find_best(')');
    if (s < 0) { msg("You have no weapon to wield."); return; }
    if (inv[s].worn) { msg("You are already wielding your best weapon."); return; }
    unworn_class(')');
    inv[s].worn = 1;
    recompute_gear();
    msg2("You wield ", obj_desc(&inv[s]), ".");
}

void do_wear(void)
{
    int s = find_best('[');
    if (s < 0) { msg("You have no armor to wear."); return; }
    if (inv[s].worn) { msg("You are already wearing your best armor."); return; }
    unworn_class('[');                  /* swap: take off the old suit first */
    inv[s].worn = 1;
    recompute_gear();
    msg2("You don ", obj_desc(&inv[s]), ".");
}

void do_puton(void)
{
    int s = find_best('=');
    if (s < 0) { msg("You have no ring to put on."); return; }
    if (inv[s].worn) { msg("You are already wearing a ring."); return; }
    unworn_class('=');
    inv[s].worn = 1;
    recompute_gear();
    msg("The ring tingles on your finger.  You feel protected.");
    sfx_magic();
}

void do_quaff(void)
{
    int s = find_class('!');
    uint8_t heal;

    if (s < 0) {
        msg("You have no potions to drink.");
        return;
    }
    heal = (uint8_t)(rn2(6) + objtypes[inv[s].otyp].prop);
    if (inv[s].otyp == O_EXHEAL) {
        if (pmaxhp < 250) pmaxhp++;
        msg("You feel much healthier!");
    } else {
        msg("You feel much better.");
    }
    php = (uint8_t)(php + heal);
    if (php > pmaxhp) php = pmaxhp;
    inv_remove((uint8_t)s);
    sfx_quaff();
}

void do_eat(void)
{
    int s = find_class('%');
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

void do_read(void)
{
    int s = find_class('?');
    uint8_t ot;

    if (s < 0) {
        msg("You have nothing to read.");
        return;
    }
    ot = inv[s].otyp;
    inv_remove((uint8_t)s);
    sfx_magic();
    if (ot == O_MAPPING) {
        fov_reveal();
        msg("A map of the level forms in your mind!");
    } else {                            /* O_TELEPORT */
        uint8_t tx, ty;
        level_random_floor(&tx, &ty);
        hero_x = tx; hero_y = ty;
        msg("You feel a wrenching sensation.");
    }
}

/* ---- save / restore (the inventory objects) ---- */

void item_save(uint8_t h)
{
    file_write(h, inv, sizeof inv);
    file_write(h, &inv_count, 1);
}

void item_load(uint8_t h)
{
    file_read(h, inv, sizeof inv);
    file_read(h, &inv_count, 1);
    recompute_gear();
}
