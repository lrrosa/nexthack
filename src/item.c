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
#include "item_int.h"   /* obj_t, inv, O_* ids -- shared with item_use.c */
#include "game.h"        /* hero_x/y, php, pmaxhp, weapon_dmg, armor_def, ac */
#include "platform.h"    /* drawing, messages, getkey, file_*                */
#include "level.h"       /* terrain, level_take_item, level_random_floor     */
#include "monster.h"     /* monster_at, hit_monster, m_sleep, pet_idx        */
#include "spells.h"      /* learn_spell ('r' on a spellbook)                 */
#include "nexthack.h"    /* build_level (wand of digging descends a level)   */
#include "rng.h"         /* rn2, world_seed                                  */
#include "sfx.h"         /* sound effects                                    */

/* item.c is a BANKED (cold) module with a bank of its OWN (it outgrew
 * PAGE_20), mapped into the 0xC000 window on demand. Public entry points are
 * __banked (see item.h); the static helpers below are reached only by in-page
 * calls, so they stay plain. Banked code may only touch RESIDENT data.
 *
 * const-banked: the object catalogue (objtypes[] with its name strings, the
 * appearance pools) and every message literal live in THIS bank, not the
 * tight resident half (~3.2 KB reclaimed). Safe because nothing outside
 * item.c reads them (audited: no external objtypes/name consumers), and
 * in-file consumers hand them only to RESIDENT callees (msg/print_str),
 * which run with this bank still mapped. Do not pass them into another
 * bank's __banked functions. */


/* ---- object catalogue (like montypes[]) ---- */

/* the O_* ids live in item_int.h: item_use.c needs them too */

/* Armour slots. Only '[' uses these; everything else is SL_NONE and the
 * field is ignored. NetHack layers a suit, and recompute_gear was already
 * summing every worn '[' piece -- one unworn_class('[') in do_wear was all
 * that kept the hero in a single suit. */
#define SL_NONE   0
#define SL_SUIT   1
#define SL_SHIELD 2
#define SL_HELM   3
#define SL_BOOTS  4
#define SL_CLOAK  5

typedef struct {
    char        cls;     /* class char in the terrain buffer                  */
    uint8_t     prop;    /* weapon:+dmg  armour:AC bonus  potion:heal base    */
    uint16_t    price;   /* base shop price (used from Phase 20)              */
    uint8_t     mindep;  /* earliest depth at which it is generated           */
    uint8_t     prob;    /* generation weight within its class (see below)   */
    uint8_t     slot;    /* '[' only: which body slot it occupies (SL_*)      */
    const char *name;
} objtype_t;

/* `prob` is a type's generation weight within its class: resolve_otyp draws
 * among the eligible types in proportion to it. Before 1.4 the draw was
 * uniform, so every new type silently diluted the old ones -- seven new rings
 * would have made protection a ninth of rings instead of a half, and a carrot
 * would have halved the food rations. A class whose types all weigh 1 still
 * resolves exactly as it did (h % n), which is what the weapons and armour
 * keep: the combat ladder is untouched by this batch. The other weights are
 * NetHack's own relative frequencies, scaled so the types this game already
 * had keep their mix among themselves. */
static const objtype_t objtypes[NUMOBJ] = {
    /* cls prop price mindep prob slot name */
    { ')',  2,    5,   1, 1, SL_NONE, "dagger" },
    { ')',  3,   15,   2, 1, SL_NONE, "short sword" },
    { ')',  4,   40,   5, 1, SL_NONE, "mace" },
    { ')',  5,   80,   9, 1, SL_NONE, "long sword" },
    { '[',  2,   10,   1, 1, SL_SUIT, "leather armor" },
    { '[',  3,   40,   3, 1, SL_SUIT, "ring mail" },
    { '[',  4,  100,   6, 1, SL_SUIT, "chain mail" },
    { '[',  5,  200,  10, 1, SL_SUIT, "plate mail" },
    { '!',  7,   20,   1, 3, SL_NONE, "potion of healing" },
    { '!', 14,   60,   4, 3, SL_NONE, "potion of extra healing" },
    { '!',  0,   30,   2, 2, SL_NONE, "potion of confusion" },
    { '!',  0,   30,   3, 2, SL_NONE, "potion of sleeping" },
    { '!',  0,   30,   4, 2, SL_NONE, "potion of blindness" },
    { '?',  0,   40,   1, 5, SL_NONE, "scroll of magic mapping" },
    { '?',  0,   60,   1, 5, SL_NONE, "scroll of teleportation" },
    { '?',  0,   40,   2, 5, SL_NONE, "scroll of identify" },
    { '=',  1,  150,   3, 1, SL_NONE, "ring of protection" },
    { '%',  0,   10,   1, 8, SL_NONE, "food ration" },
    { '"',  0,    0,  50, 1, SL_NONE, "the Amulet of Yendor" },
    /* wands: prop is unused (the effect is by type); zapped with 'z', charges
     * live in obj_t.ench. */
    { '/',  0,  150,   2, 4, SL_NONE, "wand of striking" },
    { '/',  0,  200,   4, 4, SL_NONE, "wand of cold" },
    { '/',  0,  175,   3, 4, SL_NONE, "wand of sleep" },
    { '/',  0,  200,   5, 4, SL_NONE, "wand of teleportation" },
    { '/',  0,  150,   6, 4, SL_NONE, "wand of digging" },
    { '%',  0,    2, 255, 1, SL_NONE, "corpse" },  /* never generated (mindep 255) */
    /* spellbooks: prop = the spell index (spells.c). Read to learn, Z casts. */
    { '&',  0,  100,   2, 1, SL_NONE, "spellbook of force bolt" },
    { '&',  1,  120,   3, 1, SL_NONE, "spellbook of healing" },
    { '&',  2,  150,   4, 1, SL_NONE, "spellbook of sleep" },
    { '&',  3,  180,   6, 1, SL_NONE, "spellbook of teleportation" },
    { ')',  8,  400, 255, 1, SL_NONE, "Excalibur" },  /* only from a fountain (mindep 255) */
    /* the v0.9 arsenal (appended; see the enum note) */
    { '?',  0,  100,   3, 5, SL_NONE, "scroll of enchant weapon" },
    { '?',  0,  100,   4, 5, SL_NONE, "scroll of enchant armor" },
    { '?',  0,   80,   3, 5, SL_NONE, "scroll of remove curse" },
    { '!',  0,   80,   5, 2, SL_NONE, "potion of gain level" },
    { '=',  0,  200,   6, 1, SL_NONE, "ring of regeneration" },
    { '*',  0,  300, 255, 1, SL_NONE, "luckstone" },  /* the mines bottom (levelgen) */
    /* The catalogue used to stop at plate mail on Dlvl 10 while the dungeon
     * kept scaling for forty more floors -- the hero fought the second half
     * in first-half armour. This continues the cadence the first four set
     * (+1 every three to six floors) instead of inventing a new one. Names
     * stay <= 12 chars so the 32-column inventory still fits a prefix. */
    { '[',  6,  300,  14, 1, SL_SUIT, "splint mail" },
    { '[',  7,  450,  20, 1, SL_SUIT, "banded mail" },
    { '[',  8, 1200,  28, 1, SL_SUIT, "dragon scale" },
    /* The trimmings. Each is worth about a point, as in NetHack, where the
     * body suit is most of your protection and the rest is the set. Names
     * stay <= 12 chars for the 32-column inventory. */
    { '[',  2,   60,   4, 1, SL_SHIELD, "small shield" },
    { '[',  2,   80,   8, 1, SL_HELM, "helmet" },
    { '[',  2,   70,  11, 1, SL_BOOTS, "boots" },
    { '[',  2,  120,  15, 1, SL_CLOAK, "cloak" },
    { '[',  3,  260,  20, 1, SL_SHIELD, "large shield" },
    /* Amulets. The Amulet of Yendor keeps '"' to itself on its own floor
     * (resolve_floor checks the depth), so these are what the class means
     * anywhere else. Neither has a prop: their worth is the effect. */
    { '"',  0,  180,   6, 1, SL_NONE, "amulet of ESP" },
    { '"',  0,  400,  12, 1, SL_NONE, "amulet of life" },
    /* The 1.4 rings. None armours you (recompute_gear): each is an effect,
     * read through ring_fx by whoever it concerns. Equal weights, as in
     * NetHack -- which makes protection a ninth of rings instead of a half;
     * tools/balance.py measures what that costs. Prices are NetHack's, so
     * a shop's price still says something about an unknown ring. */
    { '=',  0,  200,   3, 1, SL_NONE, "ring of slow digestion" },
    { '=',  0,  200,   5, 1, SL_NONE, "ring of free action" },
    { '=',  0,  300,   6, 1, SL_NONE, "ring of teleport control" },
    { '=',  0,  100,   3, 1, SL_NONE, "ring of stealth" },
    { '=',  0,  100,   3, 1, SL_NONE, "ring of hunger" },
    { '=',  0,  150,   3, 1, SL_NONE, "ring of aggravate monster" },
    { '=',  0,  200,   4, 1, SL_NONE, "ring of teleportitis" },
    /* Three wands, weighted by NetHack's own frequencies against the five
     * already here (4 each): opening is the second answer to a locked door,
     * after the boot -- and the key the tools class cannot give yet. */
    { '/',  0,  150,   3, 2, SL_NONE, "wand of opening" },
    { '/',  0,  175,   5, 3, SL_NONE, "wand of fire" },
    { '/',  0,  150,   2, 4, SL_NONE, "wand of magic missile" },
    /* Four scrolls, at NetHack's rarity against the six already here (5
     * each): genocide and charging are the rare prizes, destroy armor and
     * amnesia the risks that make reading an unknown scroll a decision. */
    { '?',  0,  300,   8, 1, SL_NONE, "scroll of genocide" },
    { '?',  0,  300,   4, 1, SL_NONE, "scroll of charging" },
    { '?',  0,  100,   2, 2, SL_NONE, "scroll of destroy armor" },
    { '?',  0,  200,   3, 2, SL_NONE, "scroll of amnesia" },
    /* Two potions. The potion weights are 3 for the two healing kinds and 2
     * for the rest, so each healing potion is still exactly 1/6 of all
     * potions, as when there were six: at equal weights the healing share
     * fell from a third to a quarter, and tools/balance.py measured that
     * alone halving the Valkyrie's wins (22% -> 11% with the dog). The room
     * for the newcomers comes out of confusion, sleeping and blindness.
     * NetHack weighs healing above the average potion too. Restore ability
     * is not here: nothing in this game ever lowers an attribute, so it would
     * restore nothing. It waits for a drain to answer. */
    { '!',  0,  300,   4, 2, SL_NONE, "potion of gain ability" },
    { '!',  0,  150,   3, 2, SL_NONE, "potion of gain energy" },
    /* The carrot cures blindness. Rare against the ration (8:1), as in
     * NetHack, so it does not thin the food supply much: a food drop is
     * worth 717 nutrition on average instead of 800. */
    { '%',  0,    7,   1, 1, SL_NONE, "carrot" }
};

/* obj_t, the BUC bits and inv[] live in item_int.h (item_use.c needs them).
 * inv_count is no longer static for the same reason; it is BSS, so resident
 * and readable from any bank. */
uint8_t inv_count;

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

/* ---- persistent floor stashes (v0.10) ----
 * Dropped and thrown items used to vanish on a level change. Now an LRU pool
 * remembers the floor of the STASH_SLOTS most recently left levels (keyed by
 * dlvl, like the fog-of-war pool): floor_reset() banks the outgoing level's
 * floor_obj[] on entry to build_level, and floor_restore() (called at its
 * end, once the terrain is final) re-lays the incoming level's stash --
 * deterministic regeneration guarantees each item's cell still matches its
 * remembered och. The pool is saved (item_save), so stashes survive S. */
#define STASH_SLOTS 8
typedef struct { uint8_t lvl, n; uint16_t tick; floor_t it[MAXFLOOR]; } stash_t;
static stash_t  stash[STASH_SLOTS];   /* ~544 B resident BSS (the reclaim pays) */
static uint16_t stash_clock;
static uint8_t  stash_prev;           /* dlvl whose floor sits in floor_obj[] */

/* ---- stolen goods (the nymph, v0.11) ----
 * A thief that steals keeps the item in a per-slot pocket. It comes back:
 * kill her and it drops at her feet (drop_held); leave the level or save,
 * and held_dump makes every thief abandon hers on the floor first, so the
 * loot rides the stash pool above -- stolen goods are never silently lost
 * with the transient monsters. (~60 B resident BSS.) */
static obj_t   held_obj[MAXMON];
static uint8_t held_has[MAXMON];
static void    held_dump(void);       /* defined with the nymph code, below */

static void stash_store(void)
{
    uint8_t i, v = STASH_SLOTS, free_ = STASH_SLOTS;
    if (stash_prev == 0) return;      /* nothing banked yet (boot / new game) */
    for (i = 0; i < STASH_SLOTS; i++) {
        if (stash[i].lvl == stash_prev) { v = i; break; }
        if (stash[i].lvl == 0 && free_ == STASH_SLOTS) free_ = i;
    }
    if (v == STASH_SLOTS) v = (free_ != STASH_SLOTS) ? free_ : 0;
    if (v == 0 && free_ == STASH_SLOTS && stash[0].lvl != stash_prev) {
        for (i = 1; i < STASH_SLOTS; i++)          /* evict the oldest */
            if (stash[i].tick < stash[v].tick) v = i;
    }
    if (floor_n == 0) {               /* an empty floor frees the slot */
        if (stash[v].lvl == stash_prev) stash[v].lvl = 0;
        return;
    }
    stash[v].lvl  = stash_prev;
    stash[v].n    = floor_n;
    stash[v].tick = ++stash_clock;
    for (i = 0; i < floor_n; i++) stash[v].it[i] = floor_obj[i];
}

void floor_reset(void) __banked { held_dump(); stash_store(); floor_n = 0; }

/* re-lay the incoming level's stash; terrain must be final (end of build_level) */
void floor_restore(void) __banked
{
    uint8_t i, k;
    floor_n = 0;
    for (i = 0; i < STASH_SLOTS; i++)
        if (stash[i].lvl != 0 && stash[i].lvl == (uint8_t)dlvl) {
            floor_n = stash[i].n;
            for (k = 0; k < floor_n; k++) {
                floor_obj[k] = stash[i].it[k];
                lvl[floor_obj[k].y][floor_obj[k].x] =
                    (char)objtypes[floor_obj[k].o.otyp].cls;
            }
            stash[i].tick = ++stash_clock;
            break;
        }
    stash_prev = (uint8_t)dlvl;
}

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
/* what a piece is actually worth once enchantment, erosion and blessing are
 * counted -- shared by recompute_gear and do_wear so the two cannot disagree
 * about which armour is better */
static int gear_eff(const obj_t *o)
{
    int eff = (int)objtypes[o->otyp].prop + o->ench - o->ero;
    if (buc_st(o) == BUC_BLESS)      eff += 1;   /* blessed gear is better */
    else if (buc_st(o) == BUC_CURSE) eff -= 2;   /* cursed gear is a drag   */
    return eff < 0 ? 0 : eff;
}

/* A worn set saturates. Five slots each carrying a depth-scaled enchantment
 * would otherwise hand the hero armor_def 17-19 where one suit gives 10 --
 * measured at 30-99% of runs won against 1.4-9.6%, i.e. the deep floors stop
 * being dangerous at all. The cap is NetHack's diminishing returns, cheaply:
 * the DISPLAYED ac keeps falling with every piece, so a new find still reads
 * as progress even when the reduction has topped out. */
#define ARMOR_CAP 10

static void recompute_gear(void)
{
    uint8_t i, base_ac = 10, redux = 0;

    weapon_dmg = 0;
    regen_ring = 0;         /* re-derived from what is worn (never saved) */
    ring_fx = 0;
    amu_esp = amu_life = 0;
    for (i = 0; i < inv_count; i++) {
        const objtype_t *t;
        int eff;
        if (!inv[i].worn) continue;
        t = &objtypes[inv[i].otyp];
        eff = gear_eff(&inv[i]);
        if (t->cls == ')') {
            weapon_dmg = (uint8_t)eff;
        } else if (t->cls == '[') {
            if ((uint8_t)eff <= base_ac) base_ac -= (uint8_t)eff;
            if (eff > 0) redux += (uint8_t)(eff > 1 ? eff - 1 : 1);
        } else if (t->cls == '=') {
            /* Only protection armours you. Every ring used to add its
             * gear_eff, so a BLESSED ring of anything was +1 AC -- harmless
             * with two ring types, silly once there is a blessed ring of
             * hunger. (tools/balance.py mirrors this.) */
            if (inv[i].otyp == O_PROTECT) {
                if ((uint8_t)eff <= base_ac) base_ac -= (uint8_t)eff;
                redux += (uint8_t)eff;
            }
            if (inv[i].otyp == O_REGEN)
                regen_ring = 1;         /* upkeep() mends twice as fast */
            /* the 1.4 rings are pure effects; a cursed one still works (it
             * is only stuck), as in NetHack */
            if (inv[i].otyp >= O_RSLOWDIG && inv[i].otyp <= O_RTPORT)
                ring_fx |= (uint8_t)(1u << (inv[i].otyp - O_RSLOWDIG));
        } else if (t->cls == '"') {
            if (inv[i].otyp == O_AMU_ESP)  amu_esp = 1;
            if (inv[i].otyp == O_AMU_LIFE) amu_life = 1;
        }
    }
    ac = base_ac;
    armor_def = (redux > ARMOR_CAP) ? ARMOR_CAP : redux;
}

/* corrode the worn item of class cls (acid/rust from a monster): bump its
 * erosion up to a cap of 3, weakening it via recompute_gear. */
/* One worn piece of this class, chosen at random.
 *
 * Armour used to be a single suit, so "the first worn '[' in the pack" was
 * "the only one" and every caller could scan for it. With a set that reading
 * silently became "always the lowest inventory slot": acid ate the same piece
 * for ever, and a scroll of enchant armour poured every +1 into one item --
 * which, once it hit ARMOR_CAP, did nothing at all while the other four
 * stayed at +0. Both callers now come here. */
static int pick_worn(char cls)
{
    uint8_t i, n = 0;
    int pick = -1;
    for (i = 0; i < inv_count; i++)
        if (inv[i].worn && objtypes[inv[i].otyp].cls == cls) {
            n++;
            if (rn2(n) == 0) pick = (int)i;   /* reservoir: one pass, no array */
        }
    return pick;
}

void corrode_worn(char cls) __banked
{
    int i = pick_worn(cls);
    if (i < 0) return;
    if (inv[i].ero < 3) {
        inv[i].ero++;
        recompute_gear();
        msg2("Your ", objtypes[inv[i].otyp].name, " corrodes!");
    }
}

/* ---- item identification ----------------------------------------------------
 * Potions, scrolls, rings and wands start unidentified: shown by a per-game
 * random appearance ("ruby potion", "scroll labeled XYZZY", "jade ring",
 * "oak wand") until you learn the type -- by using it, by a scroll of
 * identify, or by watching it work. The appearance is derived from world_seed,
 * so it needs no storage and is stable across save/restore; id_known records
 * which types you have since learned.
 *
 * The pools hold the distinctive word only; obj_desc adds the class noun. They
 * hold more looks than the class has types, as NetHack's do, so the last
 * unknown one cannot be named by elimination. A pool must stay at least as
 * long as its class and at most SHUF_MAX. Ring words stay <= 6 letters: the
 * discoveries line "wooden ring: aggravate monster" is then exactly 32
 * columns, the 128K's whole width. */
static const char *const pot_appear[] = {
    "ruby", "blue", "fizzy", "smoky", "cloudy", "murky",
    "golden", "milky", "bubbly", "dark", "pink", "violet"
};
static const char *const scr_appear[] = {
    "XYZZY", "ELBERETH", "KIRJE", "VAS CORP", "ANDOVA", "ZELGO MER",
    "READ ME", "TEMOV", "THARR", "YUM YUM", "NR 9", "KERNOD WEL"
};
static const char *const rng_appear[] = {
    "ruby", "jade", "opal", "coral", "onyx", "topaz",
    "agate", "bronze", "wooden", "ivory", "garnet", "silver"
};
static const char *const wnd_appear[] = {
    "oak", "pine", "glass", "iron", "brass", "ebony",
    "maple", "marble", "copper", "runed", "silver", "bone"
};
#define NAPPEAR(a) ((uint8_t)(sizeof a / sizeof a[0]))
#define SHUF_MAX 12

static uint8_t id_known[(NUMOBJ + 7) / 8];   /* one "identified?" bit per otyp */
static uint8_t id_is(uint8_t otyp)  { return (id_known[otyp >> 3] >> (otyp & 7)) & 1u; }
static void    id_set(uint8_t otyp) { id_known[otyp >> 3] |= (uint8_t)(1u << (otyp & 7)); }

/* does this class wear per-game appearances? */
static uint8_t has_looks(char cls)
{
    return (uint8_t)(cls == '!' || cls == '?' || cls == '=' || cls == '/');
}

/* which of its class's types this is, counting in catalogue order. The old
 * mapping was kept by hand (the appended arsenal types "sit past their class
 * block"); counting makes every future append correct for free. */
static uint8_t cls_ordinal(uint8_t otyp)
{
    uint8_t i, o = 0;
    char cls = objtypes[otyp].cls;
    for (i = 0; i < otyp; i++)
        if (objtypes[i].cls == cls) o++;
    return o;
}

/* Position o of this game's shuffle of an n-look pool.
 *
 * It used to be a rotation, pool[(o + seed) % n], and a rotation gives the
 * whole class away the moment one look is learned: know that ruby is healing
 * and the fixed pool order names every other potion. A Fisher-Yates shuffle,
 * seeded off world_seed and the class (so the four do not share one pattern),
 * keeps each look independent. It runs its OWN xorshift -- an rn2 here would
 * shift the game's stream every time an item is named. */
static uint8_t shuffled(uint8_t o, uint8_t n, char salt)
{
    uint8_t perm[SHUF_MAX], i, j, t;
    uint16_t x = (uint16_t)((world_seed ^ ((uint16_t)(uint8_t)salt * 40503u)) | 1u);
    for (i = 0; i < n; i++) perm[i] = i;
    for (i = (uint8_t)(n - 1); i > 0; i--) {
        x ^= (uint16_t)(x << 7);
        x ^= (uint16_t)(x >> 9);
        x ^= (uint16_t)(x << 8);
        j = (uint8_t)(x % (uint8_t)(i + 1));
        t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    return perm[o < n ? o : (uint8_t)(o % n)];   /* a too-short pool repeats
                                                  * a look rather than reading
                                                  * past perm[] */
}

/* this game's look word for a type ("ruby", "XYZZY", "oak"), identified or
 * not -- the discoveries screen needs it for the types you HAVE learned */
static const char *appearance_of(uint8_t otyp)
{
    char    cls = objtypes[otyp].cls;
    uint8_t o   = cls_ordinal(otyp);
    if (cls == '!') return pot_appear[shuffled(o, NAPPEAR(pot_appear), cls)];
    if (cls == '?') return scr_appear[shuffled(o, NAPPEAR(scr_appear), cls)];
    if (cls == '=') return rng_appear[shuffled(o, NAPPEAR(rng_appear), cls)];
    if (cls == '/') return wnd_appear[shuffled(o, NAPPEAR(wnd_appear), cls)];
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
    if (o->ench != 0 && (t->cls == ')' || t->cls == '[')) {
        /* a cursed scroll of enchantment can drive it below zero now */
        uint8_t e = (uint8_t)(o->ench < 0 ? -o->ench : o->ench);
        *p++ = (o->ench < 0) ? '-' : '+';
        *p++ = (char)('0' + (e % 10));
        *p++ = ' ';
    }
    if (o->otyp == O_CORPSE) {          /* named for the fallen: "rat corpse" */
        s = mon_name((char)o->ench);
        while (*s) *p++ = *s++;
        s = " corpse";
    } else if (has_looks(t->cls) && !id_is(o->otyp)) {
        /* unidentified: this game's look plus the class noun */
        if (t->cls == '?') { s = "scroll labeled "; while (*s) *p++ = *s++; }
        s = appearance_of(o->otyp);
        while (*s) *p++ = *s++;
        s = (t->cls == '!') ? " potion" : (t->cls == '=') ? " ring" :
            (t->cls == '/') ? " wand"   : "";
    } else {
        s = t->name;
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

/* A worn ring's effect just showed -- teleport control asked where to go,
 * free action shrugged off a gaze, teleportitis blinked you away -- so its
 * look is learned, as NetHack learns it. rf is the one RF_* bit that acted;
 * the ring behind it is O_RSLOWDIG + that bit's position. */
void ring_noticed(uint8_t rf) __banked
{
    uint8_t b = 0;
    if (!(ring_fx & rf) || rf == 0) return;
    while (!(rf & 1u)) { rf >>= 1; b++; }
    id_set((uint8_t)(O_RSLOWDIG + b));
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

/* Effective luck: the hidden stat plus a steady +2 while the luckstone rides
 * in the pack (carried, not worn). Read by the to-hit roll and by prayer. */
int8_t eff_luck(void) __banked
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (inv[i].otyp == O_LUCKSTONE)
            return (int8_t)(luck + 2);
    return luck;
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
    id_set(otyp);            /* you know what you packed, as in NetHack --
                              * the Wizard's potion no longer reads "ruby" */
    if (inv_add(&o) && worn) recompute_gear();
}

static void inv_remove(uint8_t s)
{
    while ((uint8_t)(s + 1) < inv_count) { inv[s] = inv[s + 1]; s++; }
    inv_count--;
}

/* A cursed piece you have on stays on. There is no remove command, so `d` is
 * how anything comes off -- and it used to take a stuck ring or a welded suit
 * off as freely as a clean one, which made every curse a formality: P said
 * "stuck fast" and d dropped it. 1 = it is stuck (and the curse is now seen). */
static uint8_t cursed_on(uint8_t s)
{
    char cls;
    if (!inv[s].worn || buc_st(&inv[s]) != BUC_CURSE) return 0;
    inv[s].buc |= BUC_KNOWN;
    cls = objtypes[inv[s].otyp].cls;
    msg(cls == ')' ? "Your weapon is welded fast!" :
        cls == '[' ? "Your armor is welded on!"    :
        cls == '=' ? "Your ring is stuck fast!"    : "Your amulet is stuck fast!");
    return 1;
}

#ifndef __ZXNEXT
/* The ULA has 23 rows under a list's header and the pack holds 26. The
 * inventory and the drop list simply stopped at the 23rd item, so x, y and z
 * were in the pack and on no screen; select_item did not stop at all, and a
 * long enough menu drew its tail below row 23 -- over the attributes, the
 * printer buffer and the system variables. So a list pages instead: call this
 * before each entry, and past `last` it shows --More--, waits, and starts a
 * fresh page on row 1. */
static uint8_t list_row(uint8_t row, uint8_t last)
{
    if (row <= last) return row;
    print_str(0, 23, "--More--", C_CYAN | C_BRIGHT);
    in_wait_nokey();
    getkey();
    in_wait_nokey();
    for (row = 1; row <= 23; row++) clear_line(row, C_BLACK);
    return 1;
}
#else
/* The Next's menus list one item a row from row 4, so a long one runs past
 * the status bar (rows 22-23, which the next draw_status repaints) into rows
 * 24-31, which nothing repaints: the help pointer was overwritten and the
 * tail of the list stayed on screen for the rest of the game. */
static void menu_tail_clear(void)
{
    uint8_t row;
    for (row = 24; row < TM_H; row++) clear_line(row, C_BLACK);
    draw_help();
}
#endif

/* Offer the corpse in inventory slot s on the altar the hero stands on: the
 * NetHack #offer, folded into `d` (drop) since we have no extended commands.
 * The god's mood scales with how well the altar's alignment matches yours;
 * pleased, it grants a boon. Sets acted/turns; consumes the corpse. */
static void sacrifice(uint8_t s)
{
    uint8_t aa = altar_align((uint8_t)hero_x, (uint8_t)hero_y);
    /* 2 = co-aligned, 1 = a neutral party, 0 = crossed */
    uint8_t favour = (aa == alignment) ? 2 : (aa == 1 || alignment == 1) ? 1 : 0;

    cnt_prayers++;                  /* an offering is worship (conducts) */
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
    for (i = 0; i < STASH_SLOTS; i++) stash[i].lvl = 0;      /* no stashes yet */
    for (i = 0; i < MAXMON; i++) held_has[i] = 0;            /* no stolen goods */
    stash_clock = 0;
    stash_prev = 0;
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

/* pick a concrete object of class cls that may appear by the given depth,
 * weighted by `prob` (see the catalogue). With every weight 1 this is the old
 * elig[h % n] exactly. Two passes instead of an elig[NUMOBJ] array: the
 * catalogue grew past sixty and that array sat on the 512 B stack. */
static uint8_t resolve_otyp(char cls, uint16_t h, uint8_t depth)
{
    uint8_t i;
    uint16_t sum = 0, r;
    for (i = 0; i < NUMOBJ; i++)
        if (objtypes[i].cls == cls && objtypes[i].mindep <= depth)
            sum += objtypes[i].prob;
    if (sum == 0) {
        for (i = 0; i < NUMOBJ; i++)
            if (objtypes[i].cls == cls) return i;
        return O_FOOD;
    }
    r = (uint16_t)(h % sum);
    for (i = 0; i < NUMOBJ; i++)
        if (objtypes[i].cls == cls && objtypes[i].mindep <= depth) {
            if (r < objtypes[i].prob) return i;
            r = (uint16_t)(r - objtypes[i].prob);
        }
    return O_FOOD;                      /* unreachable: r < sum */
}

/* The two curse rules the 1.4 batch brought, for every ring and scroll the
 * game makes -- the floor's (resolve_floor) and a kill's loot (death_drop),
 * which used to hand out every junk ring uncursed, so it came off as easily
 * as it went on.
 *
 * NetHack curses the bad rings nine times in ten, and that is what makes an
 * unknown ring a gamble rather than a free sample: the junk ones usually stick
 * until a remove curse, a prayer or an altar lets go. Scrolls may be blessed
 * or cursed, NetHack's one in eight each. It matters to four of them:
 * enchantment runs backwards, remove curse fails, charging drains, genocide
 * summons. */
static void gen_buc(obj_t *o, uint16_t h)
{
    if (o->otyp >= O_RHUNGER && o->otyp <= O_RTPORT && ((h >> 7) % 10u) != 0)
        o->buc = BUC_CURSE;
    if (objtypes[o->otyp].cls == '?') {
        uint8_t r = (uint8_t)((h >> 11) & 7);
        o->buc = (r == 6) ? BUC_CURSE : (r == 7) ? BUC_BLESS : BUC_UNC;
    }
}

/* resolve the concrete object lying at (x,y) - shared by the "you see here"
 * look and by pickup, so they always agree. The cell's identity hash stays
 * tied to the real dlvl (so the item is stable across visits), but inside a
 * treasure vault the *quality* is resolved at a deeper effective depth. */
static void resolve_floor(uint8_t x, uint8_t y, obj_t *o)
{
    char c = terrain(x, y);
    uint16_t h = item_hash(x, y);
    uint8_t depth = (uint8_t)eff_depth();   /* mine levels resolve shallow */

    if (in_vault_room((int)x, (int)y)) {
        uint16_t d = (uint16_t)(eff_depth() + VAULT_DEPTH_BONUS);
        depth = (uint8_t)(d > MAXLVL ? MAXLVL : d);
    }

    o->ench = 0;
    o->ero  = 0;
    o->worn = 0;
    o->buc  = BUC_UNC;
    if (c == '"') {
        /* The Amulet of Yendor sits on the bottom floor and nowhere else
         * (mindep 50 keeps resolve_otyp off it anyway); a '"' anywhere
         * shallower is one of the wearable amulets. */
        o->otyp = (dlvl == DLVL_AMULET) ? O_AMULET
                                        : resolve_otyp('"', h, depth);
        /* Belt and braces: resolve_otyp falls back to the FIRST type of the
         * class when none is eligible at this depth, and the first '"' is
         * Yendor's own. levelgen now gates on eff_depth so that cannot
         * happen, but a minted Amulet hands over the game, so refuse it here
         * too rather than trust one caller to stay careful. */
        if (o->otyp == O_AMULET) o->otyp = O_AMU_ESP;
        o->buc  = (uint8_t)((((h >> 11) & 7) < 5) ? BUC_UNC : BUC_CURSE);
        return;
    }
    if (c == '*') {
        o->otyp = O_LUCKSTONE;
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
    gen_buc(o, h);
}

/* shop value of an object: catalogue base price plus a little per enchant */
static uint16_t item_price(const obj_t *o)
{
    uint16_t p = objtypes[o->otyp].price;
    if (o->ench > 0) p = (uint16_t)(p + (uint16_t)o->ench * 5u);
    return p;
}

/* ---- the nymph's trade (see the held_obj block up top) ---- */

static void drop_held_1(uint8_t mi)
{
    if (!held_has[mi]) return;
    held_has[mi] = 0;
    floor_drop(m_x[mi], m_y[mi], &held_obj[mi]);  /* lost only if the cell is taken */
}

void drop_held(uint8_t mi) __banked { drop_held_1(mi); }   /* a kill's return */

/* every thief abandons its loot where it stands -- called before the
 * outgoing floor is banked (floor_reset) and before a save writes the
 * stash (item_save), while m_x/m_y still hold this level's monsters */
static void held_dump(void)
{
    uint8_t i;
    for (i = 0; i < mcount; i++) drop_held_1(i);
}

/* The nymph's charmed bite (ATK_ITEM, monster_ai): she lifts one loose item
 * from the pack -- worn gear is strapped on -- and blinks away across the
 * level with the prize. */
void steal_item(uint8_t mi) __banked
{
    uint8_t cand[MAXINV], n = 0, i, s;
    for (i = 0; i < inv_count; i++) {
        if (inv[i].worn) continue;
        /* The win item is barred from drop and from sale for the same reason
         * -- it must not be possible to hold has_amulet while the Amulet is
         * elsewhere. She blinks across the level with what she takes, and the
         * level forgets her when you leave it, so this was the one route out
         * that stayed open. */
        if (inv[i].otyp == O_AMULET) continue;
        cand[n++] = i;
    }
    if (n == 0 || held_has[mi]) return;     /* nothing loose / her hands are full */
    s = cand[rn2(n)];
    msg2("She steals ", obj_desc(&inv[s]), "!");
    held_obj[mi] = inv[s];
    held_has[mi] = 1;
    inv_remove(s);
    {
        uint8_t x, y;
        rand_floor((uint8_t)rn2(rcount), &x, &y);
        if (lvl[y][x] == '.' && monster_at((int)x, (int)y) < 0 &&
            !shop_in_room((int)x, (int)y) &&
            !((int)x == hero_x && (int)y == hero_y)) {
            m_x[mi] = x; m_y[mi] = y;       /* she blinks away */
            map_flush = 1;                  /* +zx: she may cross the viewport */
        }
    }
}

/* A kill may leave loot besides the corpse (NetHack's death drops): a
 * depth-appropriate item rolled from the play-time RNG -- never inside
 * generation, so the deterministic streams stay untouched. */
void death_drop(uint8_t x, uint8_t y) __banked
{
    static const char pool[8] = { ')', '[', '!', '%', '?', '=', '!', '%' };
    obj_t o;
    uint16_t h = rng_next();
    o.otyp = resolve_otyp(pool[h & 7], (uint16_t)(h >> 3), (uint8_t)eff_depth());
    o.ench = 0; o.ero = 0; o.worn = 0; o.buc = BUC_UNC;
    gen_buc(&o, rng_next());            /* a fresh draw: h already chose the type */
    floor_drop(x, y, &o);
}

/* description of the item at any cell (farlook) -- the same resolution as
 * standing on it, so a spied item is exactly what pickup will find there */
const char *floor_item_desc_at(uint8_t x, uint8_t y) __banked
{
    obj_t o;
    int fi = floor_find(x, y);
    if (fi >= 0) return obj_desc(&floor_obj[fi].o);   /* a loose item you threw */
    resolve_floor(x, y, &o);
    return obj_desc(&o);
}

/* description of the item on the hero's cell (for the "You see here" message) */
const char *floor_item_desc(void) __banked
{
    return floor_item_desc_at((uint8_t)hero_x, (uint8_t)hero_y);
}

void do_pickup(void) __banked
{
    char c = terrain(hero_x, hero_y);
    obj_t o;

    int fi;

    if (c != '"' && c != ')' && c != '[' && c != '!' && c != '*' &&
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

    if (c == '"' && o.otyp == O_AMULET) {
        /* Only the real one. Since 1.3 a '"' on the floor is usually a
         * wearable amulet, and picking one up used to set has_amulet -- an
         * amulet of ESP off Dlvl 6 handed you the victory screen. */
        has_amulet = 1;
        msg("Got the Amulet!  Climb back up!");
        sfx_levelup();
    } else if (c == '*') {
        luckstone_taken = 1;            /* gen never re-places it */
        msg("The luckstone hums with fortune!");
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
                            (cls == '=') ? " (on hand)" :
                            (cls == '"') ? " (on neck)" : "";
            print_str(x, r2, w, C_CYAN | C_BRIGHT);
        }
    }

    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    menu_tail_clear();                  /* a long list ran past the status bar */
    s = (k >= 'a' && (uint8_t)(k - 'a') < inv_count) ? (k - 'a') : -1;
    if (s < 0) return;                  /* cancelled; the caller redraws */
    if (inv[s].otyp == O_AMULET) {      /* never lose the win item by drop/sale */
        msg("You dare not part with it!");
        return;
    }
    if (cursed_on((uint8_t)s)) return;  /* stuck on: not dropped, not sold */
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
    row = 1;
    for (i = 0; i < inv_count; i++) {    /* one item per row, paged (list_row) */
        char     cls = objtypes[inv[i].otyp].cls;
        uint8_t  r2  = list_row(row, 22);
        uint8_t  x;
        row = (uint8_t)(r2 + 1);
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
    if (cursed_on((uint8_t)s)) return;  /* stuck on: not dropped, not sold */
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
        for (i = 0; i < inv_count; i++) {       /* two columns of 13 */
            char cls = objtypes[inv[i].otyp].cls;
            uint8_t row = (uint8_t)(4 + (i % 13));
            uint8_t cx  = (uint8_t)(i < 13 ? 2 : 42);
            uint8_t x;
            puttile(cx, row, tile_for(cls));    /* the item's graphic tile */
            putcell((uint8_t)(cx + 2), row, (uint8_t)('a' + i), C_WHITE | C_BRIGHT);
            x = print_str((uint8_t)(cx + 3), row, " - ", C_WHITE);
            x = print_str(x, row, obj_desc(&inv[i]), C_WHITE | C_BRIGHT);
            if (inv[i].worn) {
                const char *w = (cls == ')') ? " (wielded)" :
                                (cls == '[') ? " (worn)"    :
                                (cls == '=') ? " (on hand)" :
                            (cls == '"') ? " (on neck)" : "";
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
        y = 1;
        for (i = 0; i < inv_count; i++) {   /* one column, paged (list_row) */
            char cls = objtypes[inv[i].otyp].cls;
            uint8_t row = list_row(y, 22);
            uint8_t x;
            y = (uint8_t)(row + 1);
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

/* ---- discoveries ('D'): the looks you have decoded ----
 * The four classes that wear per-game appearances, each look beside what it
 * turned out to be. One 32-col layout for both targets, like the shared help
 * screen; the main loop repaints the map afterwards. A full list (40-odd
 * lines once rings and wands joined) no longer fits one screen, so it pages. */
static void disc_top(void)
{
    uint8_t y;
    for (y = 0; y <= 21; y++)
        clear_line(y, C_BLACK);
#ifndef __ZXNEXT
    clear_line(22, C_BLACK); clear_line(23, C_BLACK);
    map_dirty = 1;                   /* restore the map + status on return */
#endif
    print_str(0, 0, "Discoveries  (any key)", C_WHITE | C_BRIGHT);
}

static void disc_wait(void)
{
    in_wait_nokey();
    getkey();
    in_wait_nokey();
}

void show_discoveries(void) __banked
{
    static const char cls_of[4] = { '!', '?', '=', '/' };
    static const char *const head[4] = { "Potions:", "Scrolls:", "Rings:", "Wands:" };
    uint8_t i, c, row = 2, any = 0;

    disc_top();
    for (c = 0; c < 4; c++) {
        uint8_t shown = 0;
        for (i = 0; i < NUMOBJ; i++) {
            uint8_t x;
            if (objtypes[i].cls != cls_of[c] || !id_is(i)) continue;
            /* never strand a heading on the last line of a page */
            if (row > (uint8_t)(shown ? 20 : 19)) {
                print_str(1, 21, "--More--", C_CYAN | C_BRIGHT);
                disc_wait();
                disc_top();
                row = 2; shown = 0;
            }
            if (!shown) {
                print_str(1, row, head[c], C_CYAN | C_BRIGHT);
                row++; shown = 1;
            }
            /* the look, then the true name's tail: "potion of "/"scroll of "
             * are 10 chars, "ring of "/"wand of " 8 */
            x = print_str(2, row, appearance_of(i), C_YELLOW | C_BRIGHT);
            x = print_str(x, row, ": ", C_WHITE);
            print_str(x, row, objtypes[i].name + (c < 2 ? 10 : 8), C_WHITE | C_BRIGHT);
            row++; any = 1;
        }
        if (shown) row++;
    }
    if (!any)
        print_str(2, 2, "Nothing identified yet.", C_WHITE);

    disc_wait();
    clear_line(0, C_BLACK);   /* no message follows: wipe the header row, or
                               * "(any key)" lingers on the Next's msg line */
}

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

/* the piece worn in this slot, or -1 */
static int find_worn_slot(uint8_t slot)
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (inv[i].worn && objtypes[inv[i].otyp].cls == '[' &&
            objtypes[inv[i].otyp].slot == slot) return (int)i;
    return -1;
}

/* The unworn piece that improves the SET most: the biggest gain over whatever
 * already occupies its slot. One W at a time therefore assembles a suit --
 * shield, then helm, then boots -- instead of swapping the body armour, and
 * it never asks a question the old one-suit version did not ask. */
static int find_best_gain(void)
{
    int best = -1, bestgain = 0;
    uint8_t i;
    for (i = 0; i < inv_count; i++) {
        const objtype_t *t = &objtypes[inv[i].otyp];
        int gain, w;
        if (t->cls != '[' || inv[i].worn) continue;
        w = find_worn_slot(t->slot);
        gain = gear_eff(&inv[i]) - (w >= 0 ? gear_eff(&inv[w]) : 0);
        if (gain > bestgain) { bestgain = gain; best = (int)i; }
    }
    return best;
}

void do_wear(void) __banked
{
    int s = find_best_gain(), w;
    if (s < 0) {
        msg(find_best('[') < 0 ? "You have no armor to wear."
                               : "You are wearing your best.");
        return;
    }
    w = find_worn_slot(objtypes[inv[s].otyp].slot);
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg("Your armor is welded on!"); return;
    }
    if (w >= 0) inv[w].worn = 0;        /* only THAT slot comes off */
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;
    recompute_gear();
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Welded!  ", obj_desc(&inv[s]), ".");
    else                             msg2("You don ", obj_desc(&inv[s]), ".");
}

/* The amulet of life saving, checked in ONE place: main() asks before it acts
 * on `dead`, so every way of dying -- a bite, a trap, starvation, a dragon's
 * breath -- is covered without touching any of them. The amulet burns out
 * doing it, as in NetHack. Returns 1 if it spent itself. */
uint8_t life_saved(void) __banked
{
    uint8_t i;
    if (!amu_life) return 0;
    for (i = 0; i < inv_count; i++)
        if (inv[i].worn && inv[i].otyp == O_AMU_LIFE) { inv_remove(i); break; }
    dead = 0;
    php = pmaxhp;
    recompute_gear();                 /* clears amu_life with the amulet */
    /* <= 32 columns on the 128K ULA: the first wording ran to 40 and the
     * message line clipped it mid-word */
    msg("But wait...  your amulet glows!");
    sfx_magic();
    return 1;
}

/* 'P' puts on a ring or an amulet -- one key for all the jewellery, as in
 * NetHack. It used to choose "the best ring" by itself, which was harmless
 * while both rings wore their names and would now be an oracle: with the
 * looks unknown, choosing for the player tells him which ruby ring is worth
 * wearing. So it asks, through the same menu as q/r/z (silent when you carry
 * one type). One ring on the hand at a time. It charges its own turn, like
 * q/e/r, so a cancel costs none -- mainentry used to charge one and the
 * amulet path a second. */
void do_puton(void) __banked
{
    int s = select_item('P'), w;
    char cls;
    if (s == -1) { msg("You have nothing to put on."); return; }
    if (s == -2) { msg("Never mind."); return; }
    if (inv[s].worn) { msg("You are already wearing that."); return; }
    cls = objtypes[inv[s].otyp].cls;
    w = find_worn(cls);
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg(cls == '=' ? "Your ring is stuck fast!" : "Your amulet is stuck fast!");
        return;
    }
    if (w >= 0) inv[w].worn = 0;        /* only that hand, or that neck */
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;             /* putting it on reveals a curse */
    recompute_gear();
    /* A look is learned when the effect shows: an amulet at once (it has no
     * look to learn), protection because the AC moves. The other rings keep
     * their secret until they act, or until a scroll of identify. */
    if (cls == '"' || (inv[s].otyp == O_PROTECT && gear_eff(&inv[s]) > 0))
        id_set(inv[s].otyp);
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Stuck!  ", obj_desc(&inv[s]), ".");
    else                             msg2("Put on ", obj_desc(&inv[s]), ".");
    sfx_magic();
    acted = 1; turns++;
}

/* does inventory item i match the command's class?  'r' reads both scrolls
 * ('?') and spellbooks ('&'), NetHack-style. */
static uint8_t cls_match(uint8_t i, char cls)
{
    char c = objtypes[inv[i].otyp].cls;
    if (cls == 'P')          /* 'P': rings, and every amulet but Yendor's */
        return (uint8_t)(c == '=' || (c == '"' && inv[i].otyp != O_AMULET));
    if (cls == 'C')          /* 'C': the wand a scroll of charging tops up */
        return (uint8_t)(c == '/');
    return (uint8_t)(c == cls || (cls == '?' && c == '&'));
}

/* Pick an item of class cls. Returns its index, -1 if you have none, or -2 if
 * you cancelled. With a single type present it picks it silently; only when two
 * *different* types are carried does it pop a letter menu (NetHack-style). */
/* The prompt is a pure function of the class: every caller -- all of them in
 * item_use.c now -- asked for one class with one fixed question. Deriving it
 * HERE keeps the literal in this bank. A prompt passed in from item_use.c would
 * be a pointer into ITS bank, read by this code with this bank mapped: the
 * "never hand a const-banked literal to another bank" rule, a21f9fe. */
static const char *pick_prompt(char cls)
{
    switch (cls) {
    case '!': return "Drink which potion?";
    case '%': return "Eat what?";
    case '?': return "Read which scroll?";
    case ')': return "Throw which weapon?";
    case '/': return "Zap which wand?";
    case 'P': return "Put on what?";
    case 'C': return "Charge which wand?";
    }
    return "Which item?";
}

#ifdef __ZXNEXT
int select_item(char cls) __banked
{
    const char *prompt = pick_prompt(cls);
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
    menu_tail_clear();
    if (k >= 'a' && (uint8_t)(k - 'a') < inv_count &&
        cls_match((uint8_t)(k - 'a'), cls))
        return k - 'a';
    return -2;
}
#else
int select_item(char cls) __banked
{
    const char *prompt = pick_prompt(cls);
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

    for (row = 0; row <= 23; row++) clear_line(row, C_BLACK);   /* status too: a
                                     * long menu pages over row 23 (list_row) */
    map_dirty = 1;                   /* restore the map + status on return */
    print_str(0, 0, prompt, C_WHITE | C_BRIGHT);
    row = 2;
    for (i = 0; i < inv_count; i++) {
        uint8_t x;
        if (!cls_match(i, cls)) continue;
        row = list_row(row, 21);         /* the prompt below needs row + 1 */
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

/* ---- item.c's API for item_use.c (see item_int.h) ----
 * Thin __banked wrappers over the static helpers: item.c's own calls stay
 * direct, and only item_use.c pays for the trampoline. The two accessors hand
 * back a VALUE from the catalogue, never a pointer into it -- the catalogue is
 * in this bank, item_use.c is not. */
void    item_recompute_gear(void) __banked                { recompute_gear(); }
void    item_inv_remove(uint8_t s) __banked               { inv_remove(s); }
int     item_floor_drop(uint8_t x, uint8_t y, const obj_t *o) __banked
                                                          { return floor_drop(x, y, o); }
int     item_pick_worn(char cls) __banked                 { return pick_worn(cls); }
void    item_id_set(uint8_t otyp) __banked                { id_set(otyp); }
uint8_t item_obj_prop(uint8_t otyp) __banked              { return objtypes[otyp].prop; }
char    item_obj_cls(uint8_t otyp) __banked               { return objtypes[otyp].cls; }

/* The scroll of destroy armor takes the OUTERMOST piece, as in NetHack: the
 * cloak first, then the suit, the helmet, the boots and the shield -- so a
 * cloak is armour for your armour. A cursed piece goes like any other, which
 * makes the scroll one way out of a welded suit. It lives here, not in
 * item_use.c, because it needs the slots and names of the catalogue. */
void item_destroy_armor(void) __banked
{
    static const uint8_t order[5] = { SL_CLOAK, SL_SUIT, SL_HELM, SL_BOOTS, SL_SHIELD };
    uint8_t k;
    for (k = 0; k < 5; k++) {
        int w = find_worn_slot(order[k]);
        if (w >= 0) {
            /* the bare name, not obj_desc: "Your leather armor crumbles!" is
             * 28 columns, where a prefixed one would overrun the 128K's 32 */
            msg2("Your ", objtypes[inv[w].otyp].name, " crumbles!");
            inv_remove((uint8_t)w);
            recompute_gear();
            return;
        }
    }
    msg("Your skin itches.");
}

/* The scroll of amnesia forgets a third of what you had identified. Only the
 * classes that wear looks have anything to forget. */
void item_forget_ids(void) __banked
{
    uint8_t i;
    for (i = 0; i < NUMOBJ; i++)
        if (has_looks(objtypes[i].cls) && id_is(i) && rn2(3) == 0)
            id_known[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

/* The verbs that activate or consume an item -- quaff, eat, read, throw, zap --
 * are in item_use.c, split off when this bank filled (see item_int.h). */


/* ---- save / restore (the inventory objects) ---- */

void item_save(uint8_t h) __banked
{
    held_dump();                      /* thieves abandon their loot first */
    stash_store();                    /* then bank the current floor */
    file_write(h, inv, INV_BYTES);
    file_write(h, &inv_count, 1);
    file_write(h, id_known, sizeof id_known);
    file_write(h, stash, sizeof stash);
    file_write(h, &stash_clock, 2);
}

void item_load(uint8_t h) __banked
{
    file_read(h, inv, INV_BYTES);
    file_read(h, &inv_count, 1);
    file_read(h, id_known, sizeof id_known);
    file_read(h, stash, sizeof stash);
    file_read(h, &stash_clock, 2);
    stash_prev = 0;     /* boot floor is empty: the coming build_level's
                         * store must not clobber the loaded stash */
    recompute_gear();
}
