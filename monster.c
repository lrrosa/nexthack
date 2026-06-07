/* monster.c - monster catalogue, depth-based spawning, chase AI, combat and
 * experience. Monsters live in parallel arrays (not in the terrain buffer). */

#include "monster.h"
#include "level.h"        /* terrain, walkable, rand_floor, rcount, up_x/up_y */
#include "platform.h"     /* msg/msg2, T_* tiles                              */
#include "rng.h"          /* rn2                                              */
#include "game.h"         /* hero/php/dead/dlvl, xp/xlvl, weapon_dmg/armor_def */

#define MAXMON 8

uint8_t m_x[MAXMON], m_y[MAXMON], m_alive[MAXMON];
uint8_t m_hp[MAXMON];
char    m_type[MAXMON];
uint8_t mcount;

static uint8_t mon_dead[MAXLVL + 1];     /* bit i: monster i killed */

/* ---- monster catalogue ---- */
typedef struct {
    char        ch;
    uint8_t     hp;
    uint8_t     dmg;        /* bite damage is 1..dmg          */
    uint8_t     xp;         /* experience granted for a kill  */
    uint8_t     mindepth;   /* shallowest depth it appears at */
    uint8_t     tile;
    const char *name;
} MonType;

static const MonType montypes[] = {
    { 'r',  3, 3, 1, 1, T_RAT,    "rat"    },
    { 'B',  3, 2, 1, 1, T_BAT,    "bat"    },
    { 'k',  4, 2, 2, 1, T_KOBOLD, "kobold" },
    { 'd',  6, 3, 2, 2, T_DOG,    "dog"    },
    { 'S',  6, 4, 3, 3, T_SNAKE,  "snake"  },
    { 'o',  8, 4, 4, 4, T_ORC,    "orc"    },
    { 'Z', 14, 4, 6, 6, T_ZOMBIE, "zombie" }
};
#define NMON ((uint8_t)(sizeof(montypes) / sizeof(montypes[0])))

static const MonType *mon_find(char ch)
{
    uint8_t i;
    for (i = 0; i < NMON; i++)
        if (montypes[i].ch == ch)
            return &montypes[i];
    return &montypes[0];
}

const char *mon_name(char t) { return mon_find(t)->name; }
uint8_t     mon_tile(char t) { return mon_find(t)->tile; }

int monster_at(int x, int y)
{
    uint8_t i;
    for (i = 0; i < mcount; i++)
        if (m_alive[i] && m_x[i] == x && m_y[i] == y)
            return i;
    return -1;
}

static void spawn_monster(char type)
{
    const MonType *mt = mon_find(type);
    uint8_t i, x, y;
    if (mcount >= MAXMON) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;                 /* floor only          */
    if (x == up_x && y == up_y) return;           /* keep the start clear */
    if (monster_at(x, y) >= 0) return;
    m_x[mcount] = x; m_y[mcount] = y;
    m_hp[mcount] = (uint8_t)(mt->hp + dlvl / 2);   /* tougher the deeper you go */
    m_type[mcount] = type; m_alive[mcount] = 1;
    mcount++;
}

/* pick a monster type allowed at the current depth */
static char pick_mon(void)
{
    char pool[NMON];
    uint8_t n = 0, i;
    for (i = 0; i < NMON; i++)
        if (montypes[i].mindepth <= dlvl)
            pool[n++] = montypes[i].ch;
    return n ? pool[rn2(n)] : 'r';
}

void spawn_level_monsters(void)
{
    uint8_t count = (uint8_t)(2 + dlvl);   /* more monsters the deeper you go */
    uint8_t i;
    if (count > MAXMON) count = MAXMON;
    mcount = 0;
    for (i = 0; i < count; i++)
        spawn_monster(pick_mon());
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

/* ---- experience ---- */
static void gain_xp(uint8_t amt)
{
    xp = (uint16_t)(xp + amt);
    while (xlvl < 30 && xp >= (uint16_t)xlvl * 20) {
        uint8_t gain = (uint8_t)(rn2(4) + 2);   /* 2..5 max-HP boost */
        xlvl++;
        pmaxhp = (uint8_t)(pmaxhp + gain);
        php = (uint8_t)(php + gain);
        msg("Welcome to a new experience level!");
    }
}

void attack_monster(uint8_t mi)
{
    const MonType *mt = mon_find(m_type[mi]);
    uint8_t dmg = (uint8_t)(rn2(4) + 1 + weapon_dmg);  /* 1..4 + weapon */

    turns++;
    if (m_hp[mi] <= dmg) {
        m_alive[mi] = 0;
        if (dlvl <= MAXLVL)
            mon_dead[dlvl] |= (uint8_t)(1u << mi);   /* remember the kill */
        msg2("You kill the ", mt->name, "!");
        gain_xp(mt->xp);
    } else {
        m_hp[mi] = (uint8_t)(m_hp[mi] - dmg);
        msg2("You hit the ", mt->name, ".");
    }
}

/* ---- monster turn: chase the hero, attack when adjacent ---- */

static int iabs(int v) { return v < 0 ? -v : v; }

static void monster_hits_player(uint8_t i)
{
    const MonType *mt = mon_find(m_type[i]);
    uint8_t bite = (uint8_t)(rn2(mt->dmg) + 1 + dlvl / 4);  /* harder when deep */

    if (armor_def >= bite) {                 /* armor soaks the blow */
        msg2("The ", mt->name, " misses you!");
        return;
    }
    bite = (uint8_t)(bite - armor_def);

    if (php <= bite) {
        php = 0; dead = 1;
        msg2("The ", mt->name, " kills you!");
    } else {
        php = (uint8_t)(php - bite);
        msg2("The ", mt->name, " bites you!");
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
