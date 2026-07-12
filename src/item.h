/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item.h - inventory and items: pick up, list, wield/wear/quaff/eat.
 * Floor items live in the terrain buffer as single chars:
 *   ')' weapon   '[' armor   '!' potion   '%' food
 *
 * item.c is a BANKED (cold) module: its code lives in PAGE_20_CODE, mapped into
 * the 0xC000 window on demand, so every entry point is __banked (called via the
 * trampoline from resident code). Banked code may only touch RESIDENT data. */
#ifndef ITEM_H
#define ITEM_H

#include <stdint.h>

void item_reset(void) __banked;     /* empty inventory and unequip (new game)   */
int8_t eff_luck(void) __banked;     /* luck + 2 while the luckstone is carried */
void give_item(uint8_t otyp, uint8_t worn) __banked; /* class kit: add one item  */
void floor_reset(void) __banked;    /* clear loose floor items (per level entry) */
void floor_restore(void) __banked;  /* re-lay this level's stash (end of build) */
void do_pickup(void) __banked;      /* pick up the item under the hero          */
const char *floor_item_desc(void) __banked; /* describe the item under the hero */
void show_inventory(void) __banked; /* full-screen inventory list (blocks)      */
void do_wield(void) __banked;       /* wield the first weapon                   */
void do_wear(void) __banked;        /* wear the first armor                     */
void do_quaff(void) __banked;       /* drink the first potion                   */
void do_eat(void) __banked;         /* eat the first food                       */
void do_puton(void) __banked;       /* put on the first ring                    */
void do_read(void) __banked;        /* read the first scroll                    */
void do_throw(void) __banked;       /* throw a weapon in a chosen direction     */
void do_zap(void) __banked;         /* zap a wand (magic, ranged)               */
void do_drop(void) __banked;        /* drop an item on the floor (sells it in a shop) */

void corrode_worn(char cls) __banked; /* acid/rust corrodes the worn item       */
void corpse_drop(uint8_t x, uint8_t y, char mch) __banked; /* leave a corpse    */
void steal_item(uint8_t mi) __banked; /* the nymph lifts an item + blinks away  */
void drop_held(uint8_t mi) __banked;  /* a slain thief drops its stolen loot    */
void death_drop(uint8_t x, uint8_t y) __banked; /* a kill may leave random loot */
void altar_sense(void) __banked;      /* an altar reveals carried items' BUC    */
uint8_t pray_uncurse(uint8_t all) __banked; /* a prayer lifts curses (worn/all) */

void item_save(uint8_t h) __banked; /* serialise inventory + equipment          */
void item_load(uint8_t h) __banked; /* restore them from a save file            */

#endif /* ITEM_H */
