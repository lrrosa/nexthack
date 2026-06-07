/* item.h - inventory and items: pick up, list, wield/wear/quaff/eat.
 * Floor items live in the terrain buffer as single chars:
 *   ')' weapon   '[' armor   '!' potion   '%' food */
#ifndef ITEM_H
#define ITEM_H

void item_reset(void);     /* empty inventory and unequip (new game)        */
void do_pickup(void);      /* pick up the item under the hero               */
void show_inventory(void); /* full-screen inventory list (blocks for a key) */
void do_wield(void);       /* wield the first weapon                        */
void do_wear(void);        /* wear the first armor                          */
void do_quaff(void);       /* drink the first potion                        */
void do_eat(void);         /* eat the first food                            */

#endif /* ITEM_H */
