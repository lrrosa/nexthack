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

/* item.c is a BANKED (cold) module -- all its code lives in PAGE_20_CODE,
 * mapped into the 0xC000 window on demand. Public entry points are __banked
 * (see item.h); the static helpers below are reached only by in-page calls,
 * so they stay plain. Banked code may only touch RESIDENT data. */
#pragma codeseg PAGE_20_CODE

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

/* corrode the worn item of class cls (acid/rust from a monster): bump its
 * erosion up to a cap of 3, weakening it via recompute_gear. */
void corrode_worn(char cls) __banked
{
    uint8_t i;
    for (i = 0; i < inv_count; i++) {
        if (inv[i].worn && objtypes[inv[i].otyp].cls == cls) {
            if (inv[i].ero < 3) {
                inv[i].ero++;
                recompute_gear();
                msg2("Your ", objtypes[inv[i].otyp].name, " corrodes!");
            }
            return;
        }
    }
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

void item_reset(void) __banked
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

/* vault loot resolves as if this many floors deeper: a richer type pool and
 * more enchantment, so a treasure vault really is worth breaking into. */
#define VAULT_DEPTH_BONUS 8

/* pick a concrete object of class cls that may appear by the given depth */
static uint8_t resolve_otyp(char cls, uint16_t h, uint8_t depth)
{
    uint8_t i, elig[NUMOBJ], n = 0;
    for (i = 0; i < NUMOBJ; i++)
        if (objtypes[i].cls == cls && objtypes[i].mindep <= depth)
            elig[n++] = i;
    if (n == 0) {
        for (i = 0; i < NUMOBJ; i++)
            if (objtypes[i].cls == cls) return i;
        return O_FOOD;
    }
    return elig[h % n];
}

/* resolve the concrete object lying at (x,y) - shared by the "you see here"
 * look and by pickup, so they always agree. The cell's identity hash stays
 * tied to the real dlvl (so the item is stable across visits), but inside a
 * treasure vault the *quality* is resolved at a deeper effective depth. */
static void resolve_floor(uint8_t x, uint8_t y, obj_t *o)
{
    char c = terrain(x, y);
    uint16_t h = item_hash(x, y);
    uint8_t depth = (uint8_t)dlvl;

    if (in_vault_room((int)x, (int)y)) {
        uint16_t d = (uint16_t)(dlvl + VAULT_DEPTH_BONUS);
        depth = (uint8_t)(d > MAXLVL ? MAXLVL : d);
    }

    o->ench = 0;
    o->ero  = 0;
    o->worn = 0;
    if (c == '"') {
        o->otyp = O_AMULET;
        return;
    }
    o->otyp = resolve_otyp(c, h, depth);
    if (c == ')' || c == '[') {              /* small, depth-scaled enchant */
        uint8_t roll = (uint8_t)((h >> 5) % 100u);
        if (roll < depth)                o->ench = 1;
        if (roll < (uint8_t)(depth / 3)) o->ench = 2;
    }
}

/* shop value of an object: catalogue base price plus a little per enchant */
static uint16_t item_price(const obj_t *o)
{
    uint16_t p = objtypes[o->otyp].price;
    if (o->ench > 0) p = (uint16_t)(p + (uint16_t)o->ench * 5u);
    return p;
}

/* description of the item on the hero's cell (for the "You see here" message) */
const char *floor_item_desc(void) __banked
{
    obj_t o;
    resolve_floor((uint8_t)hero_x, (uint8_t)hero_y, &o);
    return obj_desc(&o);
}

void do_pickup(void) __banked
{
    char c = terrain(hero_x, hero_y);
    obj_t o;

    if (c != '"' && c != ')' && c != '[' && c != '!' &&
        c != '%' && c != '?' && c != '=') {
        msg("There is nothing here to pick up.");
        return;
    }

    resolve_floor((uint8_t)hero_x, (uint8_t)hero_y, &o);

    /* In a shop, picking an item up buys it: confirm first (so you don't waste
     * gold by accident), pay on the spot, refuse if you can't afford it. (The
     * Amulet is never in a shop.) */
    if (c != '"' && shop_in_room(hero_x, hero_y)) {
        uint16_t price = item_price(&o);
        uint8_t  x;
        int      k;
        if (gold < price) {
            msg_num("You can't afford that (", price, " gold).");
            return;
        }
        clear_line(0, C_BLACK);                       /* compose the prompt */
        x = print_str(0, 0, "Buy ", C_WHITE | C_BRIGHT);
        x = print_str(x, 0, obj_desc(&o), C_YELLOW | C_BRIGHT);
        x = print_str(x, 0, " for ", C_WHITE | C_BRIGHT);
        x = put_uint(x, 0, price, C_YELLOW | C_BRIGHT);
        print_str(x, 0, " gold?  (y/n)", C_WHITE | C_BRIGHT);
        in_wait_nokey();                              /* release the ',' */
        k = getkey();
        if (k != 'y' && k != 'Y') { msg("You leave it on the shelf."); return; }
        if (!inv_add(&o)) { msg("Your pack is full."); return; }
        gold = (uint16_t)(gold - price);
        level_take_item((uint8_t)hero_x, (uint8_t)hero_y);
        msg_num("You buy it for ", price, " gold.");
        sfx_gold();
        return;
    }

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

/* Sell an item to the shopkeeper for half its price (only inside a shop). Shows
 * a lettered list with the sell value; any non-letter key cancels. Costs no
 * turn (a counter transaction), so monsters don't move while you haggle. */
void do_sell(void) __banked
{
    uint8_t i, row;
    int k, s;

    if (!shop_in_room(hero_x, hero_y)) {
        msg("You can only sell things inside a shop.");
        return;
    }
    if (inv_count == 0) { msg("You have nothing to sell."); return; }

    for (row = 1; row <= 21; row++) clear_line(row, C_BLACK);
    print_str(2, 2, "Sell which item?   (any other key cancels)", C_WHITE | C_BRIGHT);
    for (i = 0; i < inv_count; i++) {
        uint16_t sp = (uint16_t)(item_price(&inv[i]) / 2u);
        uint8_t  r2 = (uint8_t)(4 + i);
        uint8_t  x;
        putcell(2, r2, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
        x = print_str(3, r2, " - ", C_WHITE);
        x = print_str(x, r2, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
        x = print_str(x, r2, "   [", C_CYAN);
        x = put_uint(x, r2, sp, C_YELLOW | C_BRIGHT);
        print_str(x, r2, " gold]", C_CYAN);
    }

    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    s = (k >= 'a' && (uint8_t)(k - 'a') < inv_count) ? (k - 'a') : -1;
    if (s < 0) return;                  /* cancelled; the caller redraws */

    {
        uint16_t sp = (uint16_t)(item_price(&inv[s]) / 2u);
        if (gold > (uint16_t)(60000u - sp)) gold = 60000u;   /* clamp, 16-bit */
        else                                gold = (uint16_t)(gold + sp);
        inv_remove((uint8_t)s);
        recompute_gear();               /* in case the sold item was worn */
        msg_num("You sell it for ", sp, " gold.");
        sfx_gold();
    }
}

void show_inventory(void) __banked
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
            puttile(cx, row, tile_for(cls));    /* the item's graphic tile */
            putcell((uint8_t)(cx + 2), row, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
            x = print_str((uint8_t)(cx + 3), row, " - ", C_WHITE);
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

void do_wield(void) __banked
{
    int s = find_best(')');
    if (s < 0) { msg("You have no weapon to wield."); return; }
    if (inv[s].worn) { msg("You are already wielding your best weapon."); return; }
    unworn_class(')');
    inv[s].worn = 1;
    recompute_gear();
    msg2("You wield ", obj_desc(&inv[s]), ".");
}

void do_wear(void) __banked
{
    int s = find_best('[');
    if (s < 0) { msg("You have no armor to wear."); return; }
    if (inv[s].worn) { msg("You are already wearing your best armor."); return; }
    unworn_class('[');                  /* swap: take off the old suit first */
    inv[s].worn = 1;
    recompute_gear();
    msg2("You don ", obj_desc(&inv[s]), ".");
}

void do_puton(void) __banked
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

/* Pick an item of class cls. Returns its index, -1 if you have none, or -2 if
 * you cancelled. With a single type present it picks it silently; only when two
 * *different* types are carried does it pop a letter menu (NetHack-style). */
static int select_item(char cls, const char *prompt)
{
    int first = -1;
    uint8_t i, row, multi = 0;
    int k;

    for (i = 0; i < inv_count; i++) {
        if (objtypes[inv[i].otyp].cls != cls) continue;
        if (first < 0) first = i;
        else if (inv[i].otyp != inv[first].otyp) multi = 1;
    }
    if (first < 0) return -1;
    if (!multi)    return first;

    for (row = 1; row <= 21; row++) clear_line(row, C_BLACK);
    print_str(2, 2, prompt, C_WHITE | C_BRIGHT);
    row = 4;
    for (i = 0; i < inv_count; i++) {
        uint8_t x;
        if (objtypes[inv[i].otyp].cls != cls) continue;
        putcell(2, row, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
        x = print_str(3, row, " - ", C_WHITE);
        print_str(x, row, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
        row++;
    }
    print_str(2, (uint8_t)(row + 1),
              "(press the letter, any other key cancels)", C_CYAN | C_BRIGHT);

    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    if (k >= 'a' && (uint8_t)(k - 'a') < inv_count &&
        objtypes[inv[k - 'a'].otyp].cls == cls)
        return k - 'a';
    return -2;
}

void do_quaff(void) __banked
{
    int s = select_item('!', "Drink which potion?");
    uint8_t heal;

    if (s == -1) { msg("You have no potions to drink."); return; }
    if (s == -2) { msg("Never mind."); return; }
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
    acted = 1; turns++;
}

void do_eat(void) __banked
{
    int s = select_item('%', "Eat what?");
    if (s == -1) { msg("You have nothing to eat."); return; }
    if (s == -2) { msg("Never mind."); return; }
    if (nutrition > 1200) {
        msg("You are too full to eat now.");
        return;
    }
    nutrition += 800;
    if (nutrition > 1500) nutrition = 1500;
    inv_remove((uint8_t)s);
    msg("You finish your meal.  Delicious!");
    sfx_eat();
    acted = 1; turns++;
}

void do_read(void) __banked
{
    int s = select_item('?', "Read which scroll?");
    uint8_t ot;

    if (s == -1) { msg("You have nothing to read."); return; }
    if (s == -2) { msg("Never mind."); return; }
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
    acted = 1; turns++;
}

/* ---- save / restore (the inventory objects) ---- */

void item_save(uint8_t h) __banked
{
    file_write(h, inv, sizeof inv);
    file_write(h, &inv_count, 1);
}

void item_load(uint8_t h) __banked
{
    file_read(h, inv, sizeof inv);
    file_read(h, &inv_count, 1);
    recompute_gear();
}
