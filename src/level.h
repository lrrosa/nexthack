/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* level.h - terrain buffer, procedural generation and per-level persistence.
 *
 * Map glyphs:
 *   '-' '|' wall   '.' floor   '#' corridor   '+' door
 *   '>' '<' stairs   '$' gold   '%' food   ' ' rock (impassable)
 * (Monsters live in monster.c, not in this buffer.)
 */
#ifndef LEVEL_H
#define LEVEL_H

#include <stdint.h>

#define MAPW 80
#define MAPH 21
#define OX 0              /* map -> screen column offset            */
#define OY 1              /* map -> screen row offset (row 0 = msgs) */

extern char    lvl[MAPH][MAPW];

/* The furniture (1.8, feature.c/throne.c): a sink, a throne and a grave.
 * NetHack draws them '#', '\\' and '|', two of which this map already uses
 * for corridors and walls; the buffer only needs a char nothing else holds,
 * since every renderer draws a tile (tile_for). */
#define CH_SINK   '~'
#define CH_THRONE '\\'
#define CH_GRAVE  '`'
void place_features(void) __banked;           /* build_level, beside the altar */
void quaff_sink(void) __banked;               /* 'q' on a sink                 */
void kick_sink(uint8_t x, uint8_t y) __banked;/* 'K' beside one               */
void sink_ring(uint8_t s) __banked;           /* 'd' a ring on one            */
void grave_say(uint8_t x, uint8_t y) __banked;/* its headstone                */
void grave_dig(void) __banked;                /* dug up underfoot             */
void do_sit(void) __banked;                   /* '#' (throne.c)               */
extern uint8_t rcount;                    /* number of rooms          */
extern uint8_t up_x, up_y, dn_x, dn_y;    /* this level's stairs      */
extern uint8_t mn_x, mn_y;                /* mine entrance ('v') cell on
                                           * MINES_ENTR_DLVL          */

uint16_t eff_depth(void);   /* difficulty/loot depth (mines play shallow;
                             * resident, see level.c) */

/* terrain/walkable/tile_for and the fov_* group are HOT (resident, level.c).
 * Generation and persistence are COLD (banked, levelgen.c) and so declared
 * __banked -- callers reach them through the trampoline. save/load stay
 * resident in level.c (they touch the FOV pool). */
char    terrain(int x, int y);
int     walkable(char c);
void    rand_floor(uint8_t room, uint8_t *px, uint8_t *py) __banked;
void    level_random_floor(uint8_t *px, uint8_t *py) __banked;  /* random floor cell */
uint8_t tile_for(char c);

/* Build the terrain for the current dlvl (deterministic per depth). Gold is
 * placed but not yet filtered by persistence - call apply_gold_persistence()
 * after monsters have been spawned (so spawning sees the same map each visit). */
void gen_level(void) __banked;
void apply_gold_persistence(void) __banked;

/* pick up the gold pile at (x,y): clear it and remember it. Returns 1 if a
 * known pile was there. */
int  level_take_gold(uint8_t x, uint8_t y) __banked;

/* same for a floor item (weapon/armor/potion/food) */
void apply_item_persistence(void) __banked;
int  level_take_item(uint8_t x, uint8_t y) __banked;

/* forget all remembered gold/item pickups (new game) */
void level_reset_persistence(void) __banked;

/* ---- shops (Phase 21) ---- */
/* What a shop sells (1.8), NetHack's kinds by NetHack's odds: a side hash of
 * the world and the depth (levelgen.c shop_kind), so it is the same on every
 * visit and costs the generation nothing. Index order is shop_names[] in
 * nexthack.c. */
enum { SK_GENERAL, SK_ARMOR, SK_SCROLL, SK_POTION, SK_WEAPON, SK_FOOD,
       SK_RING, SK_WAND, SK_TOOL, SK_BOOK, SK_COUNT };
uint8_t shop_kind(void) __banked;                    /* this level's shop, SK_*     */
uint8_t shop_buys(char cls) __banked;                /* will the keeper buy a cls?  */
int  shop_in_room(int x, int y) __banked;            /* is (x,y) in the shop room? */
int  shop_keeper_xy(uint8_t *kx, uint8_t *ky) __banked;  /* keeper cell, 0 if no shop */
int  shop_rect(uint8_t *sx, uint8_t *sy, uint8_t *sw, uint8_t *sh) __banked; /* shop bounds */

/* ---- treasure vault (Phase 23) ---- */
int  level_vault_room(void) __banked;   /* this level's vault room index, or -1 */
int  in_vault_room(int x, int y) __banked;   /* is (x,y) in the vault? (better loot) */

/* ---- hand-drawn templates (Phase 24, leveltmpl.c) ---- */
uint8_t template_count(void) __banked;       /* number of templates available */
void    load_template(uint8_t idx) __banked; /* stamp template into lvl[][]+r_*[] */

/* save/restore the per-depth persistence bitmasks and the fog-of-war bitmaps
 * (banked, in levelfov.c) */
void level_save(uint8_t h) __banked;
void level_load(uint8_t h) __banked;

/* ---- dug cells (1.6, levelfov.c): the pick-axe's tunnels, per level ---- */
#define DUG_FLOOR 0x80   /* dug_add(x, y | DUG_FLOOR): the cell is '.' now (a
                          * dried fountain), not '#' */
#define DUG_DOOR  0x40   /* ...y | DUG_DOOR: a found secret door, '+' (1.8)  */
#define DUG_MARK  0xC0   /* ...y | DUG_MARK: no terrain, just remembered --
                          * a sink that has given up its ring (1.8)          */
void dug_add(uint8_t x, uint8_t y) __banked;   /* remember (x,y) as dug here   */
void dug_restore(void) __banked;               /* re-open them (build_level)   */
uint8_t dug_marked(uint8_t x, uint8_t y) __banked;  /* a DUG_MARK on (x,y) here? */

/* ---- secret doors and corridors (1.8, secret.c) ---- */
void    secrets_hide(void) __banked;          /* build_level, after gen_level  */
uint8_t secrets_search(void) __banked;        /* 's': 1 door, 2 passage found  */
void    secrets_reveal_doors(void) __banked;  /* a blessed magic mapping       */
void    secrets_underfoot(void) __banked;     /* restore: open the hero's cell */
uint8_t level_is_special(void) __banked;      /* Big Room or a template? (levelgen.c) */

/* ---- field of view (fog of war) -- banked (levelfov.c) ----
 * None of these is per-cell hot: fov_update runs once per turn; draw_map calls
 * fov_bitmap()/vis_bitmap() once per redraw and reads the bitmap inline.
 * fov_reset:   forget all explored cells (entering a level)
 * fov_update:  recompute visibility from the hero position, mark seen cells
 * fov_seen:    has this cell ever been explored? (remembered terrain)
 * fov_visible: is this cell within the hero's current view right now?       */
void fov_reset(void) __banked;
void fov_update(int hx, int hy) __banked;
int  fov_seen(int x, int y) __banked;
int  fov_visible(int x, int y) __banked;
const uint8_t *fov_bitmap(void) __banked;   /* current level's explored bitmap */
const uint8_t *vis_bitmap(void) __banked;   /* cells visible this turn (1 bit/cell) */
void fov_reveal(void) __banked;             /* mark the whole current level as explored */
void fov_reveal_built(void) __banked;       /* ...all but its solid rock (attract demo) */
void fov_forget(void) __banked;             /* amnesia: forget the current level's map  */
uint8_t fov_reveal_char(char c) __banked;  /* remember every cell holding c; how many */
extern uint16_t fov_vis_sum;                /* rolling hash of vis_now; unchanged = FOV static */

uint8_t altar_align(uint8_t x, uint8_t y);  /* 0 Law / 1 Neu / 2 Cha (resident, pure hash) */

#endif /* LEVEL_H */
