/* monster.c - monster spawning, greedy chase AI and turn-based combat. */

#include "monster.h"
#include "level.h"        /* terrain, walkable, rand_floor, rcount, up_x/up_y */
#include "platform.h"     /* msg2                                             */
#include "rng.h"          /* rn2                                              */
#include "game.h"         /* hero_x/y, php, dead, dlvl, MAXLVL                */

#define MAXMON 8

uint8_t m_x[MAXMON], m_y[MAXMON], m_alive[MAXMON];
uint8_t m_hp[MAXMON];
char    m_type[MAXMON];
uint8_t mcount;

static uint8_t mon_dead[MAXLVL + 1];     /* bit i: monster i killed */

int monster_at(int x, int y)
{
    uint8_t i;
    for (i = 0; i < mcount; i++)
        if (m_alive[i] && m_x[i] == x && m_y[i] == y)
            return i;
    return -1;
}

const char *mon_name(char t)
{
    return (t == 'd') ? "dog" : "rat";
}

static void spawn_monster(char type, uint8_t hp)
{
    uint8_t i, x, y;
    if (mcount >= MAXMON) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;                 /* floor only          */
    if (x == up_x && y == up_y) return;           /* keep the start clear */
    if (monster_at(x, y) >= 0) return;
    m_x[mcount] = x; m_y[mcount] = y;
    m_hp[mcount] = hp; m_type[mcount] = type; m_alive[mcount] = 1;
    mcount++;
}

void spawn_level_monsters(void)
{
    mcount = 0;
    spawn_monster('r', 3);
    spawn_monster('r', 3);
    spawn_monster('d', 6);
    if (dlvl >= 3) spawn_monster('r', 3);
    if (dlvl >= 5) spawn_monster('d', 6);
}

void apply_monster_persistence(void)
{
    uint8_t b;
    if (dlvl > MAXLVL) return;
    for (b = 0; b < mcount; b++)
        if (mon_dead[dlvl] & (uint8_t)(1u << b))
            m_alive[b] = 0;
}

void monster_reset_persistence(void)
{
    uint8_t i;
    for (i = 0; i <= MAXLVL; i++)
        mon_dead[i] = 0;
}

void attack_monster(uint8_t mi)
{
    uint8_t dmg = (uint8_t)(rn2(4) + 1 + weapon_dmg);  /* 1..4 + weapon */
    const char *name = mon_name(m_type[mi]);

    turns++;
    if (m_hp[mi] <= dmg) {
        m_alive[mi] = 0;
        if (dlvl <= MAXLVL)
            mon_dead[dlvl] |= (uint8_t)(1u << mi);   /* remember the kill */
        msg2("You kill the ", name, "!");
    } else {
        m_hp[mi] = (uint8_t)(m_hp[mi] - dmg);
        msg2("You hit the ", name, ".");
    }
}

/* ---- monster turn ---- */

static int iabs(int v) { return v < 0 ? -v : v; }

static void monster_hits_player(uint8_t i)
{
    uint8_t bite = (uint8_t)(rn2(3) + 1);   /* 1..3 */
    const char *name = mon_name(m_type[i]);

    if (armor_def >= bite) {                 /* armor soaks the blow */
        msg2("The ", name, " misses you!");
        return;
    }
    bite = (uint8_t)(bite - armor_def);

    if (php <= bite) {
        php = 0; dead = 1;
        msg2("The ", name, " kills you!");
    } else {
        php = (uint8_t)(php - bite);
        msg2("The ", name, " bites you!");
    }
}

static int try_mon_move(uint8_t i, int dx, int dy)
{
    int nx = (int)m_x[i] + dx;
    int ny = (int)m_y[i] + dy;
    if (!walkable(terrain(nx, ny))) return 0;
    if (nx == hero_x && ny == hero_y) return 0;   /* hero handled by attack */
    if (monster_at(nx, ny) >= 0) return 0;        /* another monster        */
    m_x[i] = (uint8_t)nx; m_y[i] = (uint8_t)ny;
    return 1;
}

static void mon_step(uint8_t i)
{
    int ddx = hero_x - (int)m_x[i];
    int ddy = hero_y - (int)m_y[i];
    int dx = (ddx > 0) ? 1 : (ddx < 0) ? -1 : 0;
    int dy = (ddy > 0) ? 1 : (ddy < 0) ? -1 : 0;

    if (iabs(ddx) <= 1 && iabs(ddy) <= 1) {   /* adjacent -> attack */
        monster_hits_player(i);
        return;
    }
    /* greedy chase: diagonal first, then slide along walls */
    if (try_mon_move(i, dx, dy)) return;
    if (dx && try_mon_move(i, dx, 0)) return;
    if (dy && try_mon_move(i, 0, dy)) return;
}

void monsters_turn(void)
{
    uint8_t i;
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i]) continue;
        mon_step(i);
        if (dead) return;
    }
}
