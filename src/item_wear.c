/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item_wear.c - putting gear on: 'w' wield, 'W' wear, 'P' put on, and the
 * amulet of life saving that burns out for you.
 *
 * Split out of item.c (2026-10-23) when its bank filled a second time --
 * PAGE_28 at 157 B on the Next, BANK_0 at 37 B on the 128K -- with the
 * inventory work (a take-off command, two ring hands, stacks) still to come.
 * The equipment verbs went because they are one subject and the take-off
 * command belongs beside them.
 *
 * Like item_use.c it runs in a bank other than the catalogue's, so it reads
 * objtypes[] only by VALUE through item_int.h (a class char, a prop, a slot)
 * and names an item only through item_obj_desc, whose buffer is resident.
 * What it hands msg/msg2 is its own literals, read by resident code while
 * this bank is still mapped. */

#include "item.h"
#include "item_int.h"    /* obj_t, inv, O_* ids, SL_*, item.c's __banked API */
#include "game.h"        /* amu_life, php, pmaxhp, dead, acted, turns        */
#include "platform.h"    /* msg, msg2                                        */
#include "sfx.h"         /* sound effects                                    */

/* index of the currently-worn item of class cls, or -1 */
static int find_worn(char cls)
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (inv[i].worn && item_obj_cls(inv[i].otyp) == cls) return i;
    return -1;
}
#define find_worn_slot(sl) item_worn_with('[', (sl))

/* Set by find_best and find_best_gain: the choice passed over a better piece
 * because a two-hander and a shield exclude each other -- so the refusal can
 * say why instead of claiming you already have your best. */
static uint8_t blocked;

/* index of the strongest item of a class (highest prop + ench - erosion), so
 * 'w'/'W' equip the best you carry regardless of inventory order.
 *
 * An artifact outranks every ordinary weapon. 'w' chooses for the player, and
 * a number cannot see what an artifact is for: a +2 long sword (7) would tie
 * Sunsword and could beat Sting, so the god's gift might never be wielded.
 * Among artifacts the number decides again. */
static int find_best(char cls)
{
    int best = -1, bestval = -999, skipped = -999;
    uint8_t i;
    uint8_t shield = (uint8_t)(cls == ')' && find_worn_slot(SL_SHIELD) >= 0);
    for (i = 0; i < inv_count; i++) {
        int v;
        if (item_obj_cls(inv[i].otyp) != cls) continue;
        v = (int)item_obj_prop(inv[i].otyp) + inv[i].ench - inv[i].ero;
        if (is_artifact(inv[i].otyp)) v += 100;
        if (shield && item_obj_slot(inv[i].otyp) == SL_TWOHAND) {
            if (v > skipped) skipped = v;     /* it needs the shield's hand */
            continue;
        }
        if (v > bestval) { bestval = v; best = i; }
    }
    blocked = (uint8_t)(skipped > bestval);
    return best;
}

static void unworn_class(char cls)   /* take off whatever of this class is worn */
{
    uint8_t i;
    for (i = 0; i < inv_count; i++)
        if (item_obj_cls(inv[i].otyp) == cls) inv[i].worn = 0;
}

void do_wield(void) __banked
{
    int w = find_worn(')'), s;
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg("Your weapon is welded fast!"); return;
    }
    s = find_best(')');
    if (s < 0 || inv[s].worn) {
        msg(blocked ? "Your shield is in the way." :
            s < 0   ? "You have no weapon to wield." : "Already wielding your best.");
        return;
    }
    unworn_class(')');
    inv[s].worn = 1;
    inv[s].buc |= BUC_KNOWN;             /* equipping reveals the curse/blessing */
    item_recompute_gear();
    if (buc_st(&inv[s]) == BUC_CURSE) msg2("Welded!  ", item_obj_desc(&inv[s]), ".");
    else                             msg2("Wield ", item_obj_desc(&inv[s]), ".");
}

/* The unworn piece that improves the SET most: the biggest gain over whatever
 * already occupies its slot. One W at a time therefore assembles a suit --
 * shield, then helm, then boots -- instead of swapping the body armour, and
 * it never asks a question the old one-suit version did not ask. */
static int find_best_gain(void)
{
    int best = -1, bestgain = 0;
    uint8_t i;
    uint8_t both = (uint8_t)(item_worn_with(')', SL_TWOHAND) >= 0);
    blocked = 0;
    for (i = 0; i < inv_count; i++) {
        uint8_t slot;
        int gain, w;
        if (item_obj_cls(inv[i].otyp) != '[' || inv[i].worn) continue;
        slot = item_obj_slot(inv[i].otyp);
        w = find_worn_slot(slot);
        gain = item_gear_eff(&inv[i]) - (w >= 0 ? item_gear_eff(&inv[w]) : 0);
        if (both && slot == SL_SHIELD) {      /* no hand left to hold it */
            if (gain > 0) blocked = 1;
            continue;
        }
        if (gain > bestgain) { bestgain = gain; best = (int)i; }
    }
    return best;
}

void do_wear(void) __banked
{
    int s = find_best_gain(), w;
    if (s < 0) {
        msg(blocked                ? "Both hands are on your weapon." :
            find_best('[') < 0     ? "You have no armor to wear."
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
    cls = item_obj_cls(inv[s].otyp);
    w = find_worn(cls);
    if (w >= 0 && buc_st(&inv[w]) == BUC_CURSE && buc_seen(&inv[w])) {
        msg(cls == '=' ? "Your ring is stuck fast!" : "Your amulet is stuck fast!");
        return;
    }
    if (w >= 0) inv[w].worn = 0;        /* only that hand, or that neck */
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
