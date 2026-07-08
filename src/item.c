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
#include "monster.h"     /* monster_at, hit_monster, m_sleep, pet_idx        */
#include "spells.h"      /* learn_spell ('r' on a spellbook)                 */
#include "nexthack.h"    /* build_level (wand of digging descends a level)   */
#include "rng.h"         /* rn2, world_seed                                  */
#include "sfx.h"         /* sound effects                                    */

/* item.c is a BANKED (cold) module -- all its code lives in PAGE_20_CODE,
 * mapped into the 0xC000 window on demand. Public entry points are __banked
 * (see item.h); the static helpers below are reached only by in-page calls,
 * so they stay plain. Banked code may only touch RESIDENT data.
 *
 * constseg: the object catalogue (objtypes[] with its name strings, the
 * appearance pools) and every message literal live in THIS bank, not the
 * tight resident half (~0.9 KB reclaimed). Safe because nothing outside
 * item.c reads them (audited: no external objtypes/name consumers), and
 * in-file consumers hand them only to RESIDENT callees (msg/print_str),
 * which run with this bank still mapped. Do not pass them into another
 * bank's __banked functions. */
#ifdef __ZXNEXT
#pragma codeseg PAGE_28_CODE   /* bank 14: item.c's own bank -- code + consts */
#pragma constseg PAGE_28_CODE
#else
#pragma codeseg BANK_0         /* the 128K's spare uncontended bank */
#pragma constseg BANK_0
#endif

#define MAXINV 24

/* ---- object catalogue (like montypes[]) ---- */

enum {
    O_DAGGER, O_SHORTSW, O_MACE, O_LONGSW,     /* ')' weapons */
    O_LEATHER, O_RINGMAIL, O_CHAIN, O_PLATE,   /* '[' armour  */
    O_HEAL, O_EXHEAL, O_CONFUSION, O_SLEEPING, O_BLINDNESS,  /* '!' potions */
    O_MAPPING, O_TELEPORT, O_IDENTIFY,         /* '?' scrolls */
    O_PROTECT,                                 /* '=' ring    */
    O_FOOD,                                    /* '%' food    */
    O_AMULET,                                  /* '"' amulet  */
    O_WSTRIKE, O_WCOLD, O_WSLEEP, O_WTELE, O_WDIG,   /* '/' wands (zap with z) */
    O_CORPSE,      /* '%' a slain monster's remains (its char rides in ench);
                    * mindep 255 = never generated as loot, only dropped */
    O_BFORCE, O_BHEAL, O_BSLEEP, O_BTELE,      /* '&' spellbooks ('r' learns,
                    * 'Z' casts; prop = the spell index in spells.c) */
    O_EXCALIBUR,   /* ')' the Lady's gift; mindep 255 = only from a fountain */
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
    { '!',  0,   30,  2, "potion of confusion" },
    { '!',  0,   30,  3, "potion of sleeping" },
    { '!',  0,   30,  4, "potion of blindness" },
    { '?',  0,   40,  1, "scroll of magic mapping" },
    { '?',  0,   60,  1, "scroll of teleportation" },
    { '?',  0,   40,  2, "scroll of identify" },
    { '=',  1,  150,  3, "ring of protection" },
    { '%',  0,   10,  1, "food ration" },
    { '"',  0,    0, 50, "the Amulet of Yendor" },
    /* wands: prop is unused (the effect is by type); zapped with 'z', charges
     * live in obj_t.ench. */
    { '/',  0,  150,  2, "wand of striking" },
    { '/',  0,  200,  4, "wand of cold" },
    { '/',  0,  175,  3, "wand of sleep" },
    { '/',  0,  200,  5, "wand of teleportation" },
    { '/',  0,  150,  6, "wand of digging" },
    { '%',  0,    2, 255, "corpse" },  /* never generated (mindep 255) */
    /* spellbooks: prop = the spell index (spells.c). Read to learn, Z casts. */
    { '&',  0,  100,  2, "spellbook of force bolt" },
    { '&',  1,  120,  3, "spellbook of healing" },
    { '&',  2,  150,  4, "spellbook of sleep" },
    { '&',  3,  180,  6, "spellbook of teleportation" },
    { ')',  8,  400, 255, "Excalibur" }   /* only from a fountain (mindep 255) */
};

typedef struct {
    uint8_t otyp;
    int8_t  ench;    /* +N enchantment (weapon dmg / armour class)        */
    uint8_t ero;     /* erosion level (rust/corrosion); 0 for now         */
    uint8_t worn;    /* 1 if this is the equipped weapon/armour/ring      */
    uint8_t buc;     /* bits 0-1: 0 uncursed/1 blessed/2 cursed; bit 2 known */
} obj_t;

#define BUC_UNC   0
#define BUC_BLESS 1
#define BUC_CURSE 2
#define BUC_KNOWN 4
#define buc_st(o)   ((o)->buc & 3)         /* the blessed/uncursed/cursed state */
#define buc_seen(o) ((o)->buc & BUC_KNOWN) /* has the player discovered it?      */

/* The inventory lives in Bank 5 (always mapped at 0x4000-0x7FFF on both targets),
 * so it costs no resident BSS. The 128K places it just past udg_bitmap (0x6800);
 * the Next, whose Bank 5 holds the tilemap at 0x6000, places it in the free tail
 * of the tile-def area (tiles end ~0x53C0, NextZXOS sysvars start at 0x5C00).
 * INV_BYTES is its true size for save/restore (sizeof of the pointer is wrong). */
#ifndef __ZXNEXT
#define inv ((obj_t *)0x6800u)
#else
#define inv ((obj_t *)0x5800u)
#endif
#define INV_BYTES (sizeof(obj_t) * MAXINV)
static uint8_t inv_count;

/* Items lying loose on the current level's floor -- thrown weapons that you can
 * walk over and pick back up. They override the deterministic floor resolution
 * at their cell: the cell is marked ')' so it draws and picks up like any item,
 * and `och` remembers the terrain under it ('.' floor or '#' corridor) to
 * restore when it is taken. Per visit only (floor_reset in build_level) -- the
 * current level is regenerated on entry, so a loose item does not survive
 * leaving the level. */
typedef struct { uint8_t x, y, och; obj_t o; } floor_t;
#define MAXFLOOR 8
static floor_t floor_obj[MAXFLOOR];
static uint8_t floor_n;

void floor_reset(void) __banked { floor_n = 0; }

static int floor_find(uint8_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < floor_n; i++)
        if (floor_obj[i].x == x && floor_obj[i].y == y) return i;
    return -1;
}

/* drop object o on (x,y) if it is plain floor/corridor and free; returns 1 if it
 * came to rest there, 0 if the spot can't hold it (a wall/door/stairs/occupied
 * cell -- the caller decides whether that means "lost" or "can't drop here"). */
static int floor_drop(uint8_t x, uint8_t y, const obj_t *o)
{
    char t = lvl[y][x];
    if ((t != '.' && t != '#' && t != '_')   /* items may rest on an altar */
        || floor_find(x, y) >= 0 || floor_n >= MAXFLOOR)
        return 0;
    floor_obj[floor_n].x = x; floor_obj[floor_n].y = y;
    floor_obj[floor_n].och = (uint8_t)t;
    floor_obj[floor_n].o = *o;
    floor_n++;
    lvl[y][x] = (char)objtypes[o->otyp].cls;   /* shows as its OWN class: a dropped
                                     * potion is '!' on the map, not a ')' -- the
                                     * tile must match what floor_pick returns */
    map_flush = 1;                   /* +zx: a thrown weapon lands cells away */
    return 1;
}

static void floor_pick(uint8_t i)    /* remove entry i, restoring its terrain */
{
    lvl[floor_obj[i].y][floor_obj[i].x] = (char)floor_obj[i].och;
    while ((uint8_t)(i + 1) < floor_n) { floor_obj[i] = floor_obj[i + 1]; i++; }
    floor_n--;
}

/* A slain monster may leave its corpse where it fell: a '%' floor item whose
 * ench carries the monster's char. floor_drop validates the cell (plain
 * floor/corridor, list not full) -- when it can't rest there, no corpse. */
void corpse_drop(uint8_t x, uint8_t y, char mch) __banked
{
    obj_t o;
    o.otyp = O_CORPSE;
    o.ench = (int8_t)mch;
    o.ero = 0; o.worn = 0; o.buc = BUC_UNC;
    floor_drop(x, y, &o);
}

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
        if (buc_st(&inv[i]) == BUC_BLESS)      eff += 1;   /* blessed gear is better */
        else if (buc_st(&inv[i]) == BUC_CURSE) eff -= 2;   /* cursed gear is a drag   */
        if (eff < 0) eff = 0;
        if (t->cls == ')') {
            weapon_dmg = (uint8_t)eff;
        } else if (t->cls == '[') {
            if ((uint8_t)eff <= base_ac) base_ac -= (uint8_t)eff;
            if (eff > 0) redux += (uint8_t)(eff > 1 ? eff - 1 : 1);
        } else if (t->cls == '=') {
            if ((uint8_t)eff <= base_ac) base_ac -= (uint8_t)eff;
            redux += (uint8_t)eff;
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

/* ---- item identification ----------------------------------------------------
 * Potions and scrolls start unidentified: shown by a per-game random appearance
 * ("ruby potion", "scroll labeled XYZZY") until you use one, which reveals that
 * whole type. The appearance is a deterministic rotation of a small pool keyed
 * on world_seed, so it needs no storage and is stable across save/restore;
 * id_known records which types you have since learned. */
static const char *const pot_appear[] = {
    "ruby potion", "blue potion", "fizzy potion", "smoky potion", "cloudy potion"
};
static const char *const scr_appear[] = {
    "scroll labeled XYZZY", "scroll labeled ELBERETH",
    "scroll labeled KIRJE"
};
#define NPOT 5
#define NSCR 3

static uint8_t id_known[(NUMOBJ + 7) / 8];   /* one "identified?" bit per otyp */
static uint8_t id_is(uint8_t otyp)  { return (id_known[otyp >> 3] >> (otyp & 7)) & 1u; }
static void    id_set(uint8_t otyp) { id_known[otyp >> 3] |= (uint8_t)(1u << (otyp & 7)); }

/* display name: the true name once identified, else this game's appearance */
static const char *item_name(uint8_t otyp)
{
    char cls = objtypes[otyp].cls;
    if (!id_is(otyp)) {
        if (cls == '!') return pot_appear[(uint8_t)(otyp - O_HEAL + (world_seed % NPOT)) % NPOT];
        if (cls == '?') return scr_appear[(uint8_t)(otyp - O_MAPPING + (world_seed % NSCR)) % NSCR];
    }
    return objtypes[otyp].name;
}

/* a printable description with an erosion prefix and +N enchantment, e.g.
 * "+2 long sword" or "rusty chain mail". Returns a shared static buffer. */
static const char *obj_desc(const obj_t *o)
{
    static char buf[40];   /* must hold the longest combo, e.g. "blessed corroded
                            * +2 leather armor" (33) + NUL -- the 32-col displays
                            * clip it, but the buffer must not be overrun */
    const objtype_t *t = &objtypes[o->otyp];
    char *p = buf;
    const char *s;

    if (buc_seen(o)) {              /* show the BUC state once you've discovered it */
        s = (buc_st(o) == BUC_BLESS) ? "blessed " :
            (buc_st(o) == BUC_CURSE) ? "cursed "  : "";
        while (*s) *p++ = *s++;
    }
    if (o->ero) {
        s = (o->ero >= 2) ? "corroded " : "rusty ";
        while (*s) *p++ = *s++;
    }
    if (o->ench > 0 && (t->cls == ')' || t->cls == '[')) {
        *p++ = '+';
        *p++ = (char)('0' + (o->ench % 10));
        *p++ = ' ';
    }
    if (o->otyp == O_CORPSE) {          /* named for the fallen: "rat corpse" */
        s = mon_name((char)o->ench);
        while (*s) *p++ = *s++;
        s = " corpse";
    } else {
        s = item_name(o->otyp); /* true name if identified, else appearance */
    }
    while (*s) *p++ = *s++;
    if (t->cls == '/') {        /* a wand shows its remaining charges: " (N)" */
        *p++ = ' '; *p++ = '(';
        *p++ = (char)('0' + (o->ench % 10));
        *p++ = ')';
    }
    *p = 0;
    return buf;
}

/* An altar senses the blessed/cursed state of everything you carry. Stepping
 * onto one (try_move) marks every carried item's BUC as discovered at once. */
void altar_sense(void) __banked
{
    uint8_t i, any = 0;
    for (i = 0; i < inv_count; i++)
        if (!buc_seen(&inv[i])) { inv[i].buc |= BUC_KNOWN; any = 1; }
    msg(any ? "The altar reveals your items."
            : "You feel the altar's calm.");
}

/* The gods lift curses: with `all`, from every carried item; otherwise only
 * from worn gear. Returns how many items were uncursed (so a prayer can tell
 * whether it actually helped). */
uint8_t pray_uncurse(uint8_t all) __banked
{
    uint8_t i, n = 0;
    for (i = 0; i < inv_count; i++) {
        if (!all && !inv[i].worn) continue;
        if (buc_st(&inv[i]) == BUC_CURSE) {
            inv[i].buc = (uint8_t)((inv[i].buc & ~3) | BUC_UNC | BUC_KNOWN);
            n++;
        }
    }
    if (n) recompute_gear();        /* the cursed -2 penalty is lifted */
    return n;
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

/* index of the currently-worn item of class cls, or -1 */
static int find_worn(char cls)
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (inv[i].worn && objtypes[inv[i].otyp].cls == cls) return i;
    return -1;
}

static int inv_add(const obj_t *o)
{
    if (inv_count >= MAXINV) return 0;
    inv[inv_count++] = *o;
    return 1;
}

/* Hand the hero a starting item (the class kit): a plain uncursed object of
 * the given type, optionally pre-equipped. A wand arrives with 4 charges. */
void give_item(uint8_t otyp, uint8_t worn) __banked
{
    obj_t o;
    o.otyp = otyp;
    o.ench = (int8_t)(objtypes[otyp].cls == '/' ? 4 : 0);
    o.ero  = 0;
    o.worn = worn;
    o.buc  = BUC_UNC;
    if (inv_add(&o) && worn) recompute_gear();
}

static void inv_remove(uint8_t s)
{
    while ((uint8_t)(s + 1) < inv_count) { inv[s] = inv[s + 1]; s++; }
    inv_count--;
}

/* Offer the corpse in inventory slot s on the altar the hero stands on: the
 * NetHack #offer, folded into `d` (drop) since we have no extended commands.
 * The god's mood scales with how well the altar's alignment matches yours;
 * pleased, it grants a boon. Sets acted/turns; consumes the corpse. */
static void sacrifice(uint8_t s)
{
    uint8_t aa = altar_align((uint8_t)hero_x, (uint8_t)hero_y);
    /* 2 = co-aligned, 1 = a neutral party, 0 = crossed */
    uint8_t favour = (aa == alignment) ? 2 : (aa == 1 || alignment == 1) ? 1 : 0;

    inv_remove(s);                  /* the corpse is consumed on the altar */
    acted = 1; turns++;
    sfx_magic();

    if (favour == 0 && rn2(2)) {    /* a crossed altar may spurn the offering */
        msg("Your offering is spurned!");
        if (php > 2) php = (uint8_t)(php - 2);
        if (luck > -5) luck--;      /* and the insult is remembered */
        return;
    }

    /* the god is pleased -- a boon, richer on a co-aligned altar */
    if (luck < 5) luck++;
    switch (rn2((uint8_t)(favour >= 2 ? 5 : 4))) {
    case 0: {                       /* lift every curse you carry */
        uint8_t n = pray_uncurse(1);
        msg(n ? "You feel your burdens lift." : "You feel watched over.");
        break; }
    case 1: {                       /* bless + sharpen the wielded weapon */
        uint8_t i;
        for (i = 0; i < inv_count; i++)
            if (inv[i].worn && objtypes[inv[i].otyp].cls == ')') {
                inv[i].buc = BUC_BLESS | BUC_KNOWN;
                if (inv[i].ench < 5) inv[i].ench++;
                inv[i].ero = 0;
                recompute_gear();
                break;
            }
        msg("Your weapon gleams blue.");
        break; }
    case 2:                         /* mend body and spirit */
        php = pmaxhp; pw = pmaxpw;
        msg("A warm glow restores you.");
        break;
    case 3:                         /* toughen the body */
        if (pmaxhp < 250) { pmaxhp = (uint8_t)(pmaxhp + 3); php = pmaxhp; }
        msg("You feel more robust.");
        break;
    default:                        /* co-aligned bonus: deepen the spirit */
        if (pmaxpw < 60) { pmaxpw = (uint8_t)(pmaxpw + 2); pw = pmaxpw; }
        msg("Your spirit deepens.");
        break;
    }
}

/* Drop slot s at the hero's feet -- the shared tail of both do_drop menus.
 * On an altar the gods appraise the gift: the flash reveals its BUC state,
 * and a potion takes the altar's own touch -- blessed on a co-aligned altar,
 * cursed on a crossed one (the poor man's holy water). */
static void drop_at_feet(uint8_t s)
{
    obj_t o = inv[s];
    uint8_t altar = (uint8_t)(terrain(hero_x, hero_y) == '_');
    o.worn = 0;
    if (altar) {
        uint8_t aa = altar_align((uint8_t)hero_x, (uint8_t)hero_y);
        if (objtypes[o.otyp].cls == '!') {
            if (aa == alignment)                o.buc = BUC_BLESS;
            else if (aa != 1 && alignment != 1) o.buc = BUC_CURSE;
        }
        o.buc |= BUC_KNOWN;
    }
    if (!floor_drop((uint8_t)hero_x, (uint8_t)hero_y, &o)) {
        msg("You can't drop it here.");
        return;
    }
    if (altar) {
        uint8_t st = (uint8_t)(o.buc & 3);
        msg(st == BUC_BLESS ? "It flashes blue!" :
            st == BUC_CURSE ? "A black flash!" : "No flash.");
        sfx_magic();
    } else {
        msg("You drop it.");          /* name-free: it's the item you just chose */
        sfx_pick();
    }
    inv_remove(s);
    recompute_gear();
    acted = 1; turns++;
}

void item_reset(void) __banked
{
    uint8_t i;
    inv_count = 0;
    weapon_dmg = 0;
    for (i = 0; i < sizeof id_known; i++) id_known[i] = 0;   /* nothing learned yet */
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
    o->buc  = BUC_UNC;
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
    if (c == '/')                            /* a wand arrives with 3..7 charges */
        o->ench = (int8_t)(3 + ((h >> 5) % 5u));
    if (c == ')' || c == '[' || c == '=') {   /* equipment may be blessed or cursed */
        uint8_t r = (uint8_t)((h >> 11) & 7);  /* 5/8 uncursed, 2/8 cursed, 1/8 blessed */
        o->buc = (r < 5) ? BUC_UNC : (r < 7) ? BUC_CURSE : BUC_BLESS;
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
    int fi = floor_find((uint8_t)hero_x, (uint8_t)hero_y);
    if (fi >= 0) return obj_desc(&floor_obj[fi].o);   /* a loose item you threw */
    resolve_floor((uint8_t)hero_x, (uint8_t)hero_y, &o);
    return obj_desc(&o);
}

void do_pickup(void) __banked
{
    char c = terrain(hero_x, hero_y);
    obj_t o;

    int fi;

    if (c != '"' && c != ')' && c != '[' && c != '!' &&
        c != '%' && c != '?' && c != '=' && c != '/' && c != '&') {
        msg("Nothing here to pick up.");
        return;
    }

    fi = floor_find((uint8_t)hero_x, (uint8_t)hero_y);
    if (fi >= 0) {                          /* a loose item you threw -- reclaim it */
        if (!inv_add(&floor_obj[fi].o)) { msg("Your pack is full."); return; }
        msg2("Got ", obj_desc(&floor_obj[fi].o), ".");
        sfx_pick();
        floor_pick((uint8_t)fi);            /* removes it + restores the terrain */
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
        {   /* charisma haggling, NetHack-style: a plain face (Ch 10) pays a
             * small premium, a winning one bargains a real discount */
            uint8_t chx = at_cha;
            if (chx < 6)  chx = 6;
            if (chx > 16) chx = 16;
            price = (uint16_t)(price * (uint16_t)(31 - chx) / 20u);
            if (price == 0) price = 1;
        }
        if (gold < price) {
            msg_num("Too dear (", price, "g).");
            return;
        }
        clear_line(0, C_BLACK);                       /* compose the prompt */
        x = print_str(0, 0, "Buy ", C_WHITE | C_BRIGHT);
        x = print_str(x, 0, obj_desc(&o), C_YELLOW | C_BRIGHT);
        x = print_str(x, 0, " ", C_WHITE | C_BRIGHT);
        x = put_uint(x, 0, price, C_YELLOW | C_BRIGHT);
        print_str(x, 0, "g? y/n", C_WHITE | C_BRIGHT);
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
        msg("Got the Amulet!  Climb back up!");
        sfx_levelup();
    } else {
        msg2("Got ", obj_desc(&o), ".");        /* short, so 23-char names fit 32 cols */
        sfx_pick();
    }
}

/* Sell an item to the shopkeeper for half its price (only inside a shop). Shows
 * a lettered list with the sell value; any non-letter key cancels. Costs no
 * turn (a counter transaction), so monsters don't move while you haggle. */
#ifdef __ZXNEXT
void do_drop(void) __banked
{
    uint8_t i, row;
    int k, s, in_shop = shop_in_room(hero_x, hero_y);

    if (inv_count == 0) {
        msg(in_shop ? "You have nothing to sell." : "You have nothing to drop.");
        return;
    }

    for (row = 0; row <= 21; row++) clear_line(row, C_BLACK);   /* incl. msg row 0 */
    print_str(2, 2, in_shop ? "Sell which item?   (any other key cancels)"
                            : "Drop which item?   (any other key cancels)",
              C_WHITE | C_BRIGHT);
    for (i = 0; i < inv_count; i++) {
        char     cls = objtypes[inv[i].otyp].cls;
        uint8_t  r2  = (uint8_t)(4 + i);
        uint8_t  x;
        puttile(2, r2, tile_for(cls));    /* the item's graphic tile */
        putcell(4, r2, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
        x = print_str(5, r2, " - ", C_WHITE);
        x = print_str(x, r2, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
        if (in_shop) {                    /* the shop pays half the catalogue price */
            uint16_t sp = (uint16_t)(item_price(&inv[i]) / 2u);
            x = print_str(x, r2, "   [", C_CYAN);
            x = put_uint(x, r2, sp, C_YELLOW | C_BRIGHT);
            x = print_str(x, r2, " gold]", C_CYAN);
        }
        if (inv[i].worn) {                /* mark what you're currently using */
            const char *w = (cls == ')') ? " (wielded)" :
                            (cls == '[') ? " (worn)"    :
                            (cls == '=') ? " (on hand)" : "";
            print_str(x, r2, w, C_CYAN | C_BRIGHT);
        }
    }

    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    s = (k >= 'a' && (uint8_t)(k - 'a') < inv_count) ? (k - 'a') : -1;
    if (s < 0) return;                  /* cancelled; the caller redraws */
    if (inv[s].otyp == O_AMULET) {      /* never lose the win item by drop/sale */
        msg("You dare not part with it!");
        return;
    }
    if (inv[s].otyp == O_CORPSE && terrain(hero_x, hero_y) == '_') {
        sacrifice((uint8_t)s);          /* a corpse on an altar is an offering */
        return;
    }

    if (in_shop) {
        uint16_t sp = (uint16_t)(item_price(&inv[s]) / 2u);
        if (gold > (uint16_t)(60000u - sp)) gold = 60000u;   /* clamp, 16-bit */
        else                                gold = (uint16_t)(gold + sp);
        inv_remove((uint8_t)s);
        recompute_gear();               /* in case the sold item was worn */
        msg_num("You sell it for ", sp, " gold.");
        sfx_gold();
    } else {
        drop_at_feet((uint8_t)s);       /* at your feet -- or onto an altar */
    }
}
#else
void do_drop(void) __banked
{
    uint8_t i, row;
    int k, s, in_shop = shop_in_room(hero_x, hero_y);

    if (inv_count == 0) {
        msg(in_shop ? "Nothing to sell." : "Nothing to drop.");
        return;
    }

    for (row = 0; row <= 23; row++) clear_line(row, C_BLACK);   /* full screen */
    map_dirty = 1;                                              /* restore the map on return */
    print_str(0, 0, in_shop ? "Sell which?  (else cancel)"
                            : "Drop which?  (else cancel)", C_WHITE | C_BRIGHT);
    for (i = 0; i < inv_count && i < 23; i++) {    /* one item per row, rows 1..23 */
        char     cls = objtypes[inv[i].otyp].cls;
        uint8_t  r2  = (uint8_t)(1 + i);
        uint8_t  x;
        puttile(0, r2, tile_for(cls));    /* the item's graphic tile */
        putcell(2, r2, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
        x = print_str(3, r2, " ", C_WHITE);
        x = print_str(x, r2, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
        if (in_shop) {
            uint16_t sp = (uint16_t)(item_price(&inv[i]) / 2u);
            x = print_str(x, r2, " ", C_CYAN);
            x = put_uint(x, r2, sp, C_YELLOW | C_BRIGHT);
            x = print_str(x, r2, "g", C_CYAN);
        }
        if (inv[i].worn) print_str(x, r2, "*", C_CYAN | C_BRIGHT);  /* equipped */
    }

    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    clear_line(0, C_BLACK);             /* wipe the header row (a cancel shows no
                                         * message to overwrite it) */
    s = (k >= 'a' && (uint8_t)(k - 'a') < inv_count) ? (k - 'a') : -1;
    if (s < 0) return;                  /* cancelled; the caller redraws */
    if (inv[s].otyp == O_AMULET) {      /* never lose the win item by drop/sale */
        msg("You dare not part with it!");
        return;
    }
    if (inv[s].otyp == O_CORPSE && terrain(hero_x, hero_y) == '_') {
        sacrifice((uint8_t)s);          /* a corpse on an altar is an offering */
        return;
    }

    if (in_shop) {
        uint16_t sp = (uint16_t)(item_price(&inv[s]) / 2u);
        if (gold > (uint16_t)(60000u - sp)) gold = 60000u;   /* clamp, 16-bit */
        else                                gold = (uint16_t)(gold + sp);
        inv_remove((uint8_t)s);
        recompute_gear();               /* in case the sold item was worn */
        msg_num("You sell it for ", sp, " gold.");
        sfx_gold();
    } else {
        drop_at_feet((uint8_t)s);       /* at your feet -- or onto an altar */
    }
}
#endif

#ifdef __ZXNEXT
void show_inventory(void) __banked
{
    uint8_t i, y;

    for (y = 0; y <= 21; y++)        /* clear the map + message rows */
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
#else
void show_inventory(void) __banked
{
    uint8_t i, y;

    for (y = 0; y <= 23; y++)        /* full screen (status too) for the list */
        clear_line(y, C_BLACK);
    map_dirty = 1;                   /* restore the map + status on return */

    print_str(0, 0, "Inventory  (any key continues)", C_WHITE | C_BRIGHT);

    if (inv_count == 0) {
        print_str(2, 2, "Your pack is empty.", C_WHITE);
    } else {
        for (i = 0; i < inv_count && i < 23; i++) {   /* one column, rows 1..23 */
            char cls = objtypes[inv[i].otyp].cls;
            uint8_t row = (uint8_t)(1 + i);
            uint8_t x;
            puttile(0, row, tile_for(cls));    /* the item's graphic tile */
            putcell(2, row, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
            x = print_str(3, row, " ", C_WHITE);
            x = print_str(x, row, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
            if (inv[i].worn)                   /* '*' marks an equipped item */
                print_str(x, row, "*", C_CYAN | C_BRIGHT);
        }
    }

    in_wait_nokey();    /* wait for the 'i' that opened this to be released */
    getkey();           /* then wait for a fresh key press                  */
    in_wait_nokey();
    clear_line(0, C_BLACK);   /* viewing costs no turn, so no message clears the
                               * header row -- wipe it so it doesn't stick on the
                               * message line after the map is redrawn below */
    /* the caller redraws the map afterwards */
}
#endif

/* ---- equip / use ---- */

void do_wield(void) __banked
{
    int w = find_worn(')'), s;
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg("Your weapon is welded fast!"); return;
    }
    s = find_best(')');
    if (s < 0) { msg("You have no weapon to wield."); return; }
    if (inv[s].worn) { msg("Already wielding your best."); return; }
    unworn_class(')');
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;             /* equipping reveals the curse/blessing */
    recompute_gear();
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Welded!  ", obj_desc(&inv[s]), ".");
    else                             msg2("Wield ", obj_desc(&inv[s]), ".");
}

void do_wear(void) __banked
{
    int w = find_worn('['), s;
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg("Your armor is welded on!"); return;
    }
    s = find_best('[');
    if (s < 0) { msg("You have no armor to wear."); return; }
    if (inv[s].worn) { msg("Already wearing your best."); return; }
    unworn_class('[');                  /* swap: take off the old suit first */
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;
    recompute_gear();
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Welded!  ", obj_desc(&inv[s]), ".");
    else                             msg2("You don ", obj_desc(&inv[s]), ".");
}

void do_puton(void) __banked
{
    int w = find_worn('='), s;
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg("Your ring is stuck fast!"); return;
    }
    s = find_best('=');
    if (s < 0) { msg("You have no ring to put on."); return; }
    if (inv[s].worn) { msg("You are already wearing a ring."); return; }
    unworn_class('=');
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;
    recompute_gear();
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Stuck!  ", obj_desc(&inv[s]), ".");
    else                             msg("The ring tingles.  Protected!");
    sfx_magic();
}

/* does inventory item i match the command's class?  'r' reads both scrolls
 * ('?') and spellbooks ('&'), NetHack-style. */
static uint8_t cls_match(uint8_t i, char cls)
{
    char c = objtypes[inv[i].otyp].cls;
    return (uint8_t)(c == cls || (cls == '?' && c == '&'));
}

/* Pick an item of class cls. Returns its index, -1 if you have none, or -2 if
 * you cancelled. With a single type present it picks it silently; only when two
 * *different* types are carried does it pop a letter menu (NetHack-style). */
#ifdef __ZXNEXT
static int select_item(char cls, const char *prompt)
{
    int first = -1;
    uint8_t i, row, multi = 0;
    int k;

    for (i = 0; i < inv_count; i++) {
        if (!cls_match(i, cls)) continue;
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
        if (!cls_match(i, cls)) continue;
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
        cls_match((uint8_t)(k - 'a'), cls))
        return k - 'a';
    return -2;
}
#else
static int select_item(char cls, const char *prompt)
{
    int first = -1;
    uint8_t i, row, multi = 0;
    int k;

    for (i = 0; i < inv_count; i++) {
        if (!cls_match(i, cls)) continue;
        if (first < 0) first = i;
        else if (inv[i].otyp != inv[first].otyp) multi = 1;
    }
    if (first < 0) return -1;
    if (!multi)    return first;

    for (row = 0; row <= 21; row++) clear_line(row, C_BLACK);
    map_dirty = 1;                   /* restore the map on return */
    print_str(0, 0, prompt, C_WHITE | C_BRIGHT);
    row = 2;
    for (i = 0; i < inv_count; i++) {
        uint8_t x;
        if (!cls_match(i, cls)) continue;
        putcell(0, row, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
        x = print_str(1, row, " ", C_WHITE);
        print_str(x, row, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
        row++;
    }
    print_str(0, (uint8_t)(row + 1),
              "(letter, else cancel)", C_CYAN | C_BRIGHT);

    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    if (k >= 'a' && (uint8_t)(k - 'a') < inv_count &&
        cls_match((uint8_t)(k - 'a'), cls))
        return k - 'a';
    return -2;
}
#endif

/* Drink from the fountain the hero stands on. A worthy blade may draw
 * Excalibur from the depths (NetHack's dip, folded into the quaff); else the
 * water blesses or curses at random. Sets acted/turns itself. */
static void quaff_fountain(void)
{
    uint8_t i;
    acted = 1; turns++;

    /* the Lady of the Lake: a Valkyrie of level 5+ wielding a plain long sword
     * may draw Excalibur (once -- the sword becomes the artifact) */
    if (pclass == 0 && xlvl >= 5) {
        for (i = 0; i < inv_count; i++) {
            if (inv[i].worn && inv[i].otyp == O_LONGSW) {
                if (rn2(3) == 0) {
                    inv[i].otyp = O_EXCALIBUR;
                    inv[i].buc = BUC_BLESS | BUC_KNOWN;
                    inv[i].ero = 0;
                    id_set(O_EXCALIBUR);
                    recompute_gear();
                    msg("A hand offers up Excalibur!");
                    return;
                }
                break;
            }
        }
    }

    switch (rn2(6)) {
    case 0: case 1:                      /* cool, clear water */
        php = (uint8_t)(php + rn2(5) + 2);
        if (php > pmaxhp) php = pmaxhp;
        msg("The water is cool and clear.");
        break;
    case 2:                              /* murky water */
        if (intrinsics & INTR_POISON_RES) { msg("This water tastes stale."); }
        else { st_poison = (uint8_t)(st_poison + rn2(4) + 3);
               msg("Yecch!  Foul, murky water."); }
        break;
    case 3:                              /* coins glinting at the bottom */
        { uint16_t amt = (uint16_t)(rn2(30) + 5);
          gold = (uint16_t)(gold + amt);
          msg_num("You scoop up ", amt, " gold pieces."); }
        break;
    case 4:                              /* the fountain dries up */
        lvl[hero_y][hero_x] = '.';
        map_flush = 1;                   /* +zx: the '{' cell changed */
        msg("The fountain dries up!");
        break;
    default:
        msg("The water tastes flat.");
        break;
    }
}

void do_quaff(void) __banked
{
    int s;
    uint8_t ot;

    if (lvl[hero_y][hero_x] == '{') {    /* standing on a fountain: drink it? */
        int k;
        msg("Drink from the fountain? y/n");
        in_wait_nokey();
        k = getkey();
        in_wait_nokey();
        if (k == 'y' || k == 'Y') { quaff_fountain(); return; }
        /* else fall through to drinking a carried potion */
    }

    s = select_item('!', "Drink which potion?");
    if (s == -1) { msg("You have no potions to drink."); return; }
    if (s == -2) { msg("Never mind."); return; }
    ot = inv[s].otyp;
    if (ot == O_CONFUSION) {
        st_conf = (uint8_t)(st_conf + rn2(15) + 15);
        msg("Huh?  What?  Where am I?");
    } else if (ot == O_SLEEPING) {
        if (intrinsics & INTR_SLEEP_RES) {
            msg("You yawn.");           /* sleep resistance shrugs it off */
        } else {
            st_sleep = (uint8_t)(st_sleep + rn2(8) + 5);
            msg("You suddenly fall asleep!");
        }
    } else if (ot == O_BLINDNESS) {
        st_blind = (uint8_t)(st_blind + rn2(40) + 30);
        map_dirty = 1;                      /* redraw: the world goes dark */
        msg("Darkness falls around you.");
    } else {                                /* healing / extra healing */
        uint8_t heal = (uint8_t)(rn2(6) + objtypes[ot].prop);
        if (ot == O_EXHEAL) {
            if (pmaxhp < 250) pmaxhp++;
            msg("You feel much healthier!");
        } else {
            msg("You feel much better.");
        }
        php = (uint8_t)(php + heal);
        if (php > pmaxhp) php = pmaxhp;
    }
    id_set(ot);                 /* drinking it identifies the type */
    inv_remove((uint8_t)s);
    sfx_quaff();
    acted = 1; turns++;
}

/* "You are what you eat": a corpse feeds less than a ration but some flesh
 * teaches the body something -- or punishes it. The NetHack classics. */
static void eat_corpse(char mch)
{
    if (mch == 'S' || mch == 'k') {              /* poisonous flesh */
        if (intrinsics & INTR_POISON_RES)
            msg("Ecch.  No harm done.");
        else if (rn2(3) == 0) {
            intrinsics |= INTR_POISON_RES;
            msg("You feel healthy!");
        } else {
            st_poison = (uint8_t)(st_poison + 8);
            msg("Ecch - that was poisonous!");
        }
    } else if (mch == 'i') {                     /* homunculus: sleepy flesh */
        if (intrinsics & INTR_SLEEP_RES)
            msg("You eat.  Chewy.");
        else if (rn2(2)) {
            intrinsics |= INTR_SLEEP_RES;
            msg("You feel wide awake!");
        } else {
            st_sleep = (uint8_t)(st_sleep + rn2(4) + 3);
            msg("You doze off...");
        }
    } else if (mch == 'e') {                     /* floating eye: the classic */
        intrinsics |= INTR_TELEPATHY;
        map_dirty = 1;               /* if already blind, sense them at once */
        msg("You feel a strange awareness!");
    } else if (mch == 'a') {                     /* acid blob: burns going down */
        if (php > 2) php = (uint8_t)(php - 2);
        msg("Acrid!  It burns.");
    } else if (mch == 'D') {                     /* dragon flesh hardens the blood */
        intrinsics |= INTR_POISON_RES;
        msg("You feel healthy!");
    } else {
        msg("You eat.  Not bad.");
    }
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
    if (inv[s].otyp == O_CORPSE) {
        char mch = (char)inv[s].ench;
        inv_remove((uint8_t)s);
        nutrition += 250;                    /* lean fare next to a ration */
        if (nutrition > 1500) nutrition = 1500;
        eat_corpse(mch);
    } else {
        nutrition += 800;
        if (nutrition > 1500) nutrition = 1500;
        inv_remove((uint8_t)s);
        msg("You eat.  Delicious!");
    }
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
    if (objtypes[ot].cls == '&') {      /* a spellbook: study it (it survives) */
        learn_spell(objtypes[ot].prop);
        acted = 1; turns++;
        return;
    }
    id_set(ot);                 /* reading it identifies the type */
    inv_remove((uint8_t)s);
    sfx_magic();
    if (ot == O_MAPPING) {
        fov_reveal();
        map_flush = 1;   /* +zx: seen-bits changed but vis didn't -- show the map */
        msg("The level map fills your mind!");
    } else if (ot == O_IDENTIFY) {
        /* NetHack's blessed identify, simplified for the Z80: the whole pack.
         * Every carried type is learned and every BUC state revealed. */
        uint8_t i;
        for (i = 0; i < inv_count; i++) {
            id_set(inv[i].otyp);
            inv[i].buc |= BUC_KNOWN;
        }
        msg("You feel knowledgeable!");
    } else {                            /* O_TELEPORT */
        uint8_t tx, ty;
        level_random_floor(&tx, &ty);
        hero_x = tx; hero_y = ty;
        msg("You feel a wrenching sensation.");
    }
    acted = 1; turns++;
}

/* read one movement key into a unit direction; 0 if it was not a direction */
static int read_dir(int *dx, int *dy)
{
    int k;
    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    *dx = 0; *dy = 0;
    switch (k) {
        case 'h': case  8: *dx = -1; break;
        case 'l': case  9: *dx = +1; break;
        case 'j': case 10: *dy = +1; break;
        case 'k': case 11: *dy = -1; break;
        case 'y': *dx = -1; *dy = -1; break;
        case 'u': *dx = +1; *dy = -1; break;
        case 'b': *dx = -1; *dy = +1; break;
        case 'n': *dx = +1; *dy = +1; break;
        default: return 0;
    }
    return 1;
}

/* Throw a carried weapon in a chosen direction. It flies in a straight line up
 * to THROW_RANGE cells, passing over the pet and the shopkeeper, until it
 * strikes the first enemy (damage by the weapon's power) or a wall. It then
 * lands on the floor where it came to rest and can be walked over and picked
 * back up (floor_drop), unless it stopped on rough terrain. Your wielded weapon
 * is thrown only after a confirmation, so you don't disarm yourself by mistake. */
#define THROW_RANGE 8
void do_throw(void) __banked
{
    int s = select_item(')', "Throw which weapon?");
    int dx, dy, x, y, r;
    uint8_t dmg, worn;
    obj_t thrown;

    if (s == -1) { msg("You have no weapon to throw."); return; }
    if (s == -2) { msg("Never mind."); return; }

    worn = inv[s].worn;
    if (worn) {                          /* don't disarm yourself by accident */
        int k;
        msg("Throw your wielded weapon?  y/n");
        in_wait_nokey();
        k = getkey();
        if (k != 'y' && k != 'Y') { msg("Never mind."); return; }
    }

    msg("In what direction?");
    if (!read_dir(&dx, &dy)) { msg("Never mind."); return; }

    thrown = inv[s];                     /* keep a copy: it lands on the floor */
    thrown.worn = 0;                     /* a weapon on the floor is not wielded */
    dmg = (uint8_t)(objtypes[thrown.otyp].prop
                    + (thrown.ench > 0 ? thrown.ench : 0) + rn2(3));

    x = hero_x; y = hero_y;
    for (r = 0; r < THROW_RANGE; r++) {
        int nx = x + dx, ny = y + dy, mi;
        if (!walkable(terrain(nx, ny))) break;        /* a wall ahead: stop here */
        x = nx; y = ny;
        mi = monster_at(x, y);
        if (mi < 0) continue;
        if (mi == pet_idx || m_type[mi] == MON_KEEPER) continue;  /* fly past */
        hit_monster((uint8_t)mi, dmg);
        break;                                         /* lands at the enemy's feet */
    }
    inv_remove((uint8_t)s);
    if (worn) recompute_gear();          /* you just threw what you were wielding */
    floor_drop((uint8_t)x, (uint8_t)y, &thrown);       /* leave it to be reclaimed */
    sfx_hit();
    acted = 1; turns++;
}

/* Zap a wand. Digging bores straight down (you drop a level); the others fire a
 * bolt in a chosen direction that flies up to ZAP_RANGE cells, over the pet and
 * the shopkeeper, and acts on the monster(s) it meets: striking damages the
 * first, cold chills every monster in the line, sleep dozes the first, and
 * teleportation whisks the first away. Each zap spends a charge (obj_t.ench). */
#define ZAP_RANGE 9
void do_zap(void) __banked
{
    int s = select_item('/', "Zap which wand?");
    int dx, dy, x, y, r, hit = 0;
    uint8_t ot;

    if (s == -1) { msg("You have no wand to zap."); return; }
    if (s == -2) { msg("Never mind."); return; }
    if (inv[s].ench <= 0) { msg("The wand has no charge."); return; }
    ot = inv[s].otyp;

    if (ot == O_WDIG) {                  /* digging needs no aim -- it goes down */
        inv[s].ench--;
        acted = 1; turns++;
        if (dlvl >= DLVL_AMULET) { msg("The floor here resists digging."); return; }
        msg("You dig a hole and drop through!");
        sfx_stairs();
        dlvl++;
        build_level();
        hero_x = up_x; hero_y = up_y;
        place_pet();
        return;
    }

    msg("In what direction?");
    if (!read_dir(&dx, &dy)) { msg("Never mind."); return; }
    inv[s].ench--;                       /* a real zap spends a charge */
    sfx_magic();

    x = hero_x; y = hero_y;
    for (r = 0; r < ZAP_RANGE; r++) {
        int mi;
        x += dx; y += dy;
        if (!walkable(terrain(x, y))) break;             /* a wall stops the bolt */
        mi = monster_at(x, y);
        if (mi < 0) continue;
        if (mi == pet_idx || m_type[mi] == MON_KEEPER) continue;  /* spare dog/keeper */
        hit = 1;
        if (ot == O_WSTRIKE) { hit_monster((uint8_t)mi, (uint8_t)(rn2(8) + 3)); break; }
        if (ot == O_WCOLD)   { hit_monster((uint8_t)mi, (uint8_t)(rn2(6) + 2)); continue; }
        if (ot == O_WSLEEP)  { m_sleep[mi] = (uint8_t)(rn2(10) + 8);
                               msg2("The ", mon_name(m_type[mi]), " falls asleep."); break; }
        /* O_WTELE: whisk the monster to a random spot, off your back */
        { uint8_t tx, ty, t = 0;
          do { rand_floor((uint8_t)rn2(rcount), &tx, &ty); }
          while (tx == (uint8_t)hero_x && ty == (uint8_t)hero_y && t++ < 8);
          m_x[mi] = tx; m_y[mi] = ty;
          msg2("The ", mon_name(m_type[mi]), " vanishes!"); }
        break;
    }
    if (!hit) msg("The bolt fizzles out.");
    acted = 1; turns++;
}

/* ---- save / restore (the inventory objects) ---- */

void item_save(uint8_t h) __banked
{
    file_write(h, inv, INV_BYTES);
    file_write(h, &inv_count, 1);
    file_write(h, id_known, sizeof id_known);
}

void item_load(uint8_t h) __banked
{
    file_read(h, inv, INV_BYTES);
    file_read(h, &inv_count, 1);
    file_read(h, id_known, sizeof id_known);
    recompute_gear();
}
