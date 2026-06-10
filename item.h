/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item.h - inventory and items: pick up, list, wield/wear/quaff/eat.
 * Floor items live in the terrain buffer as single chars:
 *   ')' weapon   '[' armor   '!' potion   '%' food */
#ifndef ITEM_H
#define ITEM_H

#include <stdint.h>

void item_reset(void);     /* empty inventory and unequip (new game)        */
void do_pickup(void);      /* pick up the item under the hero               */
void show_inventory(void); /* full-screen inventory list (blocks for a key) */
void do_wield(void);       /* wield the first weapon                        */
void do_wear(void);        /* wear the first armor                          */
void do_quaff(void);       /* drink the first potion                        */
void do_eat(void);         /* eat the first food                            */
void do_puton(void);       /* put on the first ring                         */
void do_read(void);        /* read the first scroll                         */

void item_save(uint8_t h); /* serialise inventory + equipment to a save file */
void item_load(uint8_t h); /* restore them from a save file                  */

#endif /* ITEM_H */
