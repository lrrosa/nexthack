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

char    terrain(int x, int y);
int     walkable(char c);
void    rand_floor(uint8_t room, uint8_t *px, uint8_t *py);
uint8_t tile_for(char c);

/* Build the terrain for the current dlvl (deterministic per depth). Gold is
 * placed but not yet filtered by persistence - call apply_gold_persistence()
 * after monsters have been spawned (so spawning sees the same map each visit). */
void gen_level(void);
void apply_gold_persistence(void);

/* pick up the gold pile at (x,y): clear it and remember it. Returns 1 if a
 * known pile was there. */
int  level_take_gold(uint8_t x, uint8_t y);

/* forget all remembered gold pickups (new game) */
void level_reset_persistence(void);

#endif /* LEVEL_H */
