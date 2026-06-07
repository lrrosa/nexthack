/* monster.h - monsters: spawning, chase AI and combat.
 * Monsters are kept in parallel arrays (not in the terrain buffer) so they
 * can move and carry HP independently of what is drawn. */
#ifndef MONSTER_H
#define MONSTER_H

#include <stdint.h>

extern uint8_t mcount;
extern uint8_t m_x[], m_y[], m_alive[];
extern char    m_type[];

int         monster_at(int x, int y);
const char *mon_name(char t);
uint8_t     mon_tile(char t);   /* graphic tile for a monster type */

/* spawn the monsters for the current dlvl (deterministic) */
void spawn_level_monsters(void);
/* re-apply remembered kills for the current dlvl (after spawning) */
void apply_monster_persistence(void);
/* forget all remembered kills (new game) */
void monster_reset_persistence(void);

/* hero attacks monster mi (the monster strikes back on its own turn) */
void attack_monster(uint8_t mi);
/* every living monster chases the hero and attacks when adjacent */
void monsters_turn(void);

#endif /* MONSTER_H */
