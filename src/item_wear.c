/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item_wear.c - putting gear on: 'w' wield, 'W' wear, 'P' put on, and the
 * amulet of life saving that burns out for you -- and 'T', taking them off.
 *
 * Split out of item.c (2026-10-23) when its bank filled a second time --
 * PAGE_28 at 157 B on the Next, BANK_0 at 37 B on the 128K -- with the
 * inventory work (a take-off command, two ring hands, stacks) still to come.
 * The equipment verbs went because they are one subject and the take-off
 * command belongs beside them.
 *
 * Like item_use.c it runs in a bank other than the catalogue's, so it reads
 * objtypes[] only by VALUE through item_int.h (a class char, a slot) and names
 * an item only through item_obj_desc, whose buffer is resident. The two
 * choices that scan the catalogue -- the best weapon, the armour that gains
 * most -- stayed in item.c beside it (item_find_best, item_find_best_gain):
 * here, every item they weigh cost three trampoline calls, and the bytes
 * did not fit the 128K's BANK_4 once 'T' joined them.
 * What it hands msg/msg2 is its own literals, read by resident code while
 * this bank is still mapped. */

#include "item.h"
#include "item_int.h"    /* obj_t, inv, O_* ids, SL_*, item.c's __banked API */
#include "game.h"        /* amu_life, php, dead, acted, turns, st_blind      */
#include "platform.h"    /* msg, msg2                                        */
#include "sfx.h"         /* sound effects                                    */

/* index of the currently-worn item of class cls, or -1 (for a ring: the
 * first of the two) */
static int find_worn(char cls)
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (inv[i].worn && item_obj_cls(inv[i].otyp) == cls) return i;
    return -1;
}
#define find_worn_slot(sl) item_worn_with('[', (sl))

void do_wield(void) __banked
{
    int w = find_worn(')'), s;
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg("Your weapon is welded fast!"); return;
    }
    s = item_find_best(')');
    if (s < 0 || inv[s].worn) {
        msg(item_blocked ? "Your shield is in the way." :
            s < 0        ? "You have no weapon to wield." : "Already wielding your best.");
        return;
    }
    if (w >= 0) inv[w].worn = 0;         /* one weapon in hand at a time */
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;             /* equipping reveals the curse/blessing */
    item_recompute_gear();
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Welded!  ", item_obj_desc(&inv[s]), ".");
    else                             msg2("Wield ", item_obj_desc(&inv[s]), ".");
}

void do_wear(void) __banked
{
    int s = item_find_best_gain(), w;
    if (s < 0) {
        msg(item_blocked            ? "Both hands are on your weapon." :
            item_find_best('[') < 0 ? "You have no armor to wear."
                                    : "You are wearing your best.");
        return;
    }
    w = find_worn_slot(item_obj_slot(inv[s].otyp));
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg("Your armor is welded on!"); return;
    }
    if (w >= 0) inv[w].worn = 0;        /* only THAT slot comes off */
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;
    item_recompute_gear();
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Welded!  ", item_obj_desc(&inv[s]), ".");
    else                             msg2("You don ", item_obj_desc(&inv[s]), ".");
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
        if (inv[i].worn && inv[i].otyp == O_AMU_LIFE) { item_inv_remove(i); break; }
    dead = 0;
    php = pmaxhp;
    item_recompute_gear();            /* clears amu_life with the amulet */
    /* <= 32 columns on the 128K ULA: the first wording ran to 40 and the
     * message line clipped it mid-word */
    msg("But wait...  your amulet glows!");
    sfx_magic();
    return 1;
}

/* rings on your hands: two hands, so at most two */
static uint8_t rings_on(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < inv_count; i++)
        if (inv[i].worn && item_obj_cls(inv[i].otyp) == '=') n++;
    return n;
}

/* 'P' puts on a ring or an amulet -- one key for all the jewellery, as in
 * NetHack. It used to choose "the best ring" by itself, which was harmless
 * while both rings wore their names and would now be an oracle: with the
 * looks unknown, choosing for the player tells him which ruby ring is worth
 * wearing. So it asks, through the same menu as q/r/z (silent when you carry
 * one type). It charges its own turn, like q/e/r, so a cancel costs none --
 * mainentry used to charge one and the amulet path a second.
 *
 * Two rings since 1.9, one on each hand, and one amulet. Until then P swapped
 * the ring you had on for the new one, which was the only way a ring came off
 * short of dropping it. Now 'T' takes things off, so P only fills a free
 * place, as in NetHack: with two rings on, WHICH one makes way is the
 * player's choice, and a cursed one cannot. */
void do_puton(void) __banked
{
    int s = select_item('P');
    char cls;
    if (s == -1) { msg("You have nothing to put on."); return; }
    if (s == -2) { msg("Never mind."); return; }
    if (inv[s].worn) { msg("You are already wearing that."); return; }
    cls = item_obj_cls(inv[s].otyp);
    if (cls == '=' ? rings_on() >= 2 : find_worn(cls) >= 0) {
        msg(cls == '=' ? "You already wear two rings." : "You already wear an amulet.");
        return;
    }
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;             /* putting it on reveals a curse */
    item_recompute_gear();
    /* A look is learned when the effect shows: an amulet at once (it has no
     * look to learn), protection because the AC moves. The other rings keep
     * their secret until they act, or until a scroll of identify. */
    if (cls == '"' || (inv[s].otyp == O_PROTECT && item_gear_eff(&inv[s]) > 0))
        item_id_set(inv[s].otyp);
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Stuck!  ", item_obj_desc(&inv[s]), ".");
    else                             msg2("Put on ", item_obj_desc(&inv[s]), ".");
    sfx_magic();
    acted = 1; turns++;
}

/* 'T' takes off one thing you have on (1.9): a piece of armour, a ring, the
 * amulet, the blindfold, or the weapon in your hand. Until it existed `d` was
 * the only way anything came off, so a ring of hunger had to be dropped to be
 * rid of, and choosing a shield again after a two-hander meant leaving the
 * two-hander on the floor. The same menu as P (silent when one thing is on).
 * A cursed piece stays on: cursed_on says so and reveals the curse, and that
 * costs no turn, as a cancel does not; taking something off costs one. */
void do_takeoff(void) __banked
{
    int s = select_item('T');
    if (s == -1) { msg("You are not wearing anything."); return; }
    if (s == -2) { msg("Never mind."); return; }
    if (item_cursed_on((uint8_t)s)) return;
    inv[s].worn = 0;
    item_recompute_gear();
    if (item_obj_cls(inv[s].otyp) == ')') {
        msg("You are empty handed.");
    } else {
        /* the blindfold's own blindness ends with it -- not a potion's
         * (apply_blindfold, item_use.c, keeps the same rule) */
        if (inv[s].otyp == O_BLINDFOLD && st_blind <= 2) { st_blind = 0; map_dirty = 1; }
        msg2("You take off ", item_obj_desc(&inv[s]), ".");
    }
    sfx_pick();
    acted = 1; turns++;
}
