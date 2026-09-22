/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item_int.h - item.c's INTERNALS, shared with item_use.c and nobody else.
 *
 * item.c was split in two when its bank filled -- PAGE_28 had 163 B left on
 * the Next, BANK_0 240 B on the 128K -- and item_use.c took the verbs that
 * activate or consume an item. What those need from item.c lives here rather
 * than in item.h, so `inv`, obj_t and the O_* ids stay private to the two
 * files: a `#define inv` in a header half the game includes would silently
 * rewrite any local of that name.
 *
 * THE RULE THE SPLIT RESTS ON. item.c's catalogue -- objtypes[], its names and
 * appearance pools -- is const-banked in item.c's OWN bank. item_use.c runs in
 * a different one, so it must never hold a pointer into that catalogue: the
 * bytes would be read with the wrong bank paged in. So everything below
 * crosses by VALUE -- a slot, an otyp, a class char, a prop byte. The one
 * pointer that does cross, `const obj_t *`, points at DATA (inv[] in Bank 5,
 * or a stack local), which is mapped whatever bank is running.
 */
#ifndef ITEM_INT_H
#define ITEM_INT_H

#include <stdint.h>

#define MAXINV 26      /* one per menu letter a..z (the hard cap) */

/* ---- object ids (the index into item.c's objtypes[]) ---- */
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
    /* the v0.9 arsenal -- APPENDED so no earlier index shifts (classes.c's
     * kit numbers and saved otyps stay valid) */
    O_ENCHW, O_ENCHA, O_RMCURSE,               /* '?' scrolls */
    O_GAINLVL,                                 /* '!' potion  */
    O_REGEN,                                   /* '=' ring    */
    O_LUCKSTONE,   /* '*' the mines' prize; mindep 255 = placed, not generated */
    /* the 1.2 armour ladder -- APPENDED, as ever, so no saved otyp shifts.
     * (id_known is (NUMOBJ+7)/8 bytes: 36 and 39 types both need 5, so the
     * save format is untouched.) */
    O_SPLINT, O_BANDED, O_DRAGSCALE,           /* '[' the deep armour */
    O_SHIELD, O_HELM, O_BOOTS, O_CLOAK, O_LSHIELD,  /* '[' the other slots */
    O_AMU_ESP, O_AMU_LIFE,                     /* '"' amulets you can wear  */
    /* the 1.4 item batch -- appended, as ever. The rings' order is the RF_*
     * bit order in game.h (recompute_gear shifts by otyp - O_RSLOWDIG); the
     * last three are the junk that is usually generated cursed. */
    O_RSLOWDIG, O_RFREEACT, O_RTCTRL, O_RSTEALTH,  /* '=' the useful rings */
    O_RHUNGER, O_RAGGR, O_RTPORT,                  /* '=' and the junk     */
    O_WOPEN, O_WFIRE, O_WMMISSILE,                 /* '/' three more wands */
    O_SGENO, O_SCHARGE, O_SDESTROY, O_SAMNESIA,    /* '?' four more scrolls */
    O_PGAINABIL, O_PGAINENRG,                      /* '!' two more potions  */
    O_CARROT,                                      /* '%' good for the eyes */
    NUMOBJ
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
extern uint8_t inv_count;

/* item.c's API for item_use.c. __banked because the caller now runs in
 * another bank. The helpers are thin wrappers: item.c keeps calling its own
 * static versions directly, so its ~14 internal recompute_gear calls do not
 * all grow into trampoline calls. */
int     select_item(char cls) __banked;   /* the prompt follows from the class */
void    item_recompute_gear(void) __banked;
void    item_inv_remove(uint8_t s) __banked;
int     item_floor_drop(uint8_t x, uint8_t y, const obj_t *o) __banked;
int     item_pick_worn(char cls) __banked;
void    item_id_set(uint8_t otyp) __banked;
uint8_t item_obj_prop(uint8_t otyp) __banked;  /* objtypes[otyp].prop, by value */
void    item_destroy_armor(void) __banked;     /* the outermost worn piece goes  */
void    item_forget_ids(void) __banked;        /* amnesia: a third of the looks  */
char    item_obj_cls(uint8_t otyp) __banked;   /* objtypes[otyp].cls, by value  */

#endif /* ITEM_INT_H */
