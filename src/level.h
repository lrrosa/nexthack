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
extern uint8_t rcount;                    /* number of rooms          */
extern uint8_t up_x, up_y, dn_x, dn_y;    /* this level's stairs      */

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
int  shop_in_room(int x, int y) __banked;            /* is (x,y) in the shop room? */
int  shop_keeper_xy(uint8_t *kx, uint8_t *ky) __banked;  /* keeper cell, 0 if no shop */

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

#endif /* LEVEL_H */
