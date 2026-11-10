/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* wizard.c - the Wizard of Yendor (2.0). RESIDENT on the 128K and BANKED on
 * the Next; wizard.h says why.
 *
 * He wakes when the Amulet leaves Dlvl 50 (amu_taken) and comes for it, as
 * NetHack's does:
 *   - a while later he appears beside you, and while he is up he is on
 *     whatever level you are. A visit rebuilds the level's monsters, and so
 *     does a restore, so wiz_tick puts him back each time: what he carries
 *     from level to level lives in `wiz`, not in his slot.
 *   - a blow of his may snatch the Amulet (ATK_AMU, wiz_steal). He takes it
 *     to the up stairs -- the way you must go -- and dozes there until the
 *     fight comes to him: you beside him, or anything that hurts him. Then,
 *     holding it, he fights to the death. Kill him and it is yours again.
 *   - under WIZ_FLEE HP he vanishes, and comes back mended a long while
 *     later; he mends a point every turn as well, as NetHack's does.
 *   - far from you, he steps out of nowhere at your side; near, he now and
 *     then calls the dungeon's servants to you.
 *   - Elbereth and the scroll of scare monster mean nothing to him
 *     (monster_ai.c), and he cannot be genocided (item_use.c).
 *   - killed, he rises again a while later.
 * The numbers are below, where tools/balance.py reads them (with his
 * montypes[] row). They make him a thief more than a killer: measured with
 * the dog over 5 seeds, he and the Astral Plane together cost about three
 * points of every class's wins against 1.9 (the Tourist five), which the
 * user chose over a Wizard too mild to matter. The climb's deeper pool
 * (CLIMB_POOL, monster.h) could not pay for him: the bite keys off the real
 * depth, so in the model the pool's +15 is worth under 1.5 points. */

#include "wizard.h"
#include "game.h"
#include "level.h"        /* up_x/up_y, level_random_floor, shop_in_room */
#include "monster.h"      /* the slots, summon_near, pick_mon, mon_find */
#include "platform.h"     /* msg, msg_more */
#include "rng.h"
#include "item_int.h"     /* inv, O_AMULET, obj_t and item.c's __banked API */

/* turns until he comes: MIN + rng_next() % RND */
#define WIZ_WAKE_MIN    50    /* after the Amulet leaves Dlvl 50 */
#define WIZ_WAKE_RND   100
#define WIZ_AWAY_MIN  1000    /* after he vanished to mend */
#define WIZ_AWAY_RND  1000
#define WIZ_RISE_MIN  1500    /* after you killed him */
#define WIZ_RISE_RND  1000
#define WIZ_FLEE        35    /* under this many HP he vanishes to mend */
#define WIZ_STEAL        6    /* one landed blow in this takes the Amulet */
#define WIZ_SUMMON     20     /* one turn in this, near you, he summons */

wiz_t wiz;               /* resident DATA on both targets */

/* 1: the fight has come to the Wizard who holds the Amulet, and he no
 * longer dozes on the stairs (wiz_tick). to_stairs, which every holder of
 * it passes, clears it. Not saved: after a restore he dozes again until the
 * fight comes to him once more. */
static uint8_t roused;

static int iabs(int v) { return v < 0 ? -v : v; }

/* his slot on this level, or -1: there is only ever one of him */
static int8_t wiz_slot(void)
{
    uint8_t i;
    for (i = 0; i < mcount; i++)
        if (m_alive[i] && m_type[i] == WIZ_CH) return (int8_t)i;
    return -1;
}

/* how far he is from you, in king's moves */
static uint8_t wiz_dist(uint8_t i)
{
    int dx = iabs((int)m_x[i] - hero_x), dy = iabs((int)m_y[i] - hero_y);
    return (uint8_t)(dx > dy ? dx : dy);
}

/* With the Amulet he holds the up stairs, as NetHack's covetous monsters
 * do. Taken -- you are on them, or another monster is -- he blinks across
 * the level as the nymph does: never into a shop, where the chase refuses
 * to step, nor onto you (level_random_floor hands back your own square when
 * the whole level refuses). */
static void to_stairs(uint8_t i)
{
    int o = monster_at(up_x, up_y);
    uint8_t n, x = up_x, y = up_y;
    roused = 0;                          /* he dozes there until you come */
    if ((o >= 0 && o != i) || (x == (uint8_t)hero_x && y == (uint8_t)hero_y))
        for (n = 0; ; n++) {
            if (n == 4) return;
            level_random_floor(&x, &y);
            if ((x != (uint8_t)hero_x || y != (uint8_t)hero_y) && !shop_in_room(x, y))
                break;
        }
    m_x[i] = x; m_y[i] = y;
    map_flush = 1;                       /* +zx: he may cross the viewport */
}

/* summon_near puts him on a free square beside you (or fails: no room, a
 * shop all round you); his wounds are his own, not a fresh monster's */
static uint8_t place_beside(void)
{
    if (!summon_near(WIZ_CH, 1)) return 0;
    m_hp[(uint8_t)wiz_slot()] = wiz.hp;
    return 1;
}

static void gone(uint16_t min, uint16_t rnd)
{
    wiz.st    = WZ_GONE;
    wiz.timer = (uint16_t)(min + rng_next() % rnd);
}

void wiz_tick(void) WIZ_ENTRY
{
    int8_t  s;
    uint8_t i, top, hurt;

    if (!amu_taken || dead) return;
    top = mon_find(WIZ_CH)->hp;
    if (wiz.st != WZ_UP) {
        if (wiz.st == WZ_NONE)           /* the Amulet has just left its floor */
            gone(WIZ_WAKE_MIN, WIZ_WAKE_RND);
        if (--wiz.timer) return;
        wiz.hp = top;                    /* mended, or risen */
        if (!place_beside()) {           /* no room beside you: next turn, */
            wiz.timer = 1;               /* and not a word until he stands */
            return;                      /* there (ten live slots: a death) */
        }
        wiz.st = WZ_UP;
        msg(wiz.deaths && !wiz.mend ? "\"So thou thought me dead, fool?\""
                                    : "The Wizard of Yendor appears!");
        wiz.mend = 0;
        if (wiz.amu) to_stairs((uint8_t)wiz_slot());
        return;
    }

    s = wiz_slot();
    if (s < 0) {                         /* a new level, or a restore */
        if (place_beside() && wiz.amu)
            to_stairs((uint8_t)wiz_slot());
        return;                          /* (no room beside you: next turn) */
    }
    i = (uint8_t)s;
    hurt = (uint8_t)(m_hp[i] < wiz.hp);  /* struck since the last tick */
    if (m_hp[i] < top) m_hp[i]++;        /* he mends a point a turn */
    wiz.hp = m_hp[i];

    if (wiz.amu) {
        /* On the stairs with it he dozes until the fight comes to him: you
         * beside him, or anything that hurts him -- a bolt, a dagger, the
         * dog. Then he is up for good, and fights to the death as
         * balance.py's stairs fight has him do: he never flees, nor calls
         * for help, but he does step to your side. Until 2026-11-10 the doze
         * was put back every turn you stood off, so the blow that woke him
         * was undone before he could act, and a wand, a spell or the dog
         * killed him at no risk at all. */
        if (!roused) {
            if (!hurt && wiz_dist(i) > 1) { m_sleep[i] = 2; return; }
            roused = 1;
            m_sleep[i] = 0;
        }
    } else if (wiz.hp < WIZ_FLEE) {      /* hurt: away to mend */
        m_alive[i] = 0;
        map_flush  = 1;
        wiz.mend   = 1;
        gone(WIZ_AWAY_MIN, WIZ_AWAY_RND);
        msg("The Wizard of Yendor vanishes!");
        return;
    }
    if (wiz_dist(i) > 4) {
        if (rn2(3) == 0) {               /* out of nowhere, at your side */
            m_alive[i] = 0;
            if (!place_beside()) m_alive[i] = 1;
        }
    } else if (!wiz.amu && rn2(WIZ_SUMMON) == 0 &&
               summon_near(pick_mon(), (uint8_t)(1 + rn2(2)))) {
        msg("\"Destroy the thief, my pets!\"");
    }
}

/* A blow of his landed (ATK_AMU, monster_ai.c): now and then it has the
 * Amulet off you, and he is away to the stairs with it. */
void wiz_steal(uint8_t i) WIZ_ENTRY
{
    uint8_t s;
    if (!has_amulet || rn2(WIZ_STEAL)) return;
    for (s = 0; s < inv_count; s++)
        if (inv[s].otyp == O_AMULET) break;
    if (s == inv_count) return;
    wiz.buc = inv[s].buc;
    item_inv_remove(s);
    has_amulet = 0;                      /* amu_taken stays: Dlvl 50 is bare */
    wiz.amu = 1;
    msg("The Wizard snatches the Amulet!");
    to_stairs(i);
}

/* The Wizard has just died (drop_held, item.c -- every kill passes there).
 * The Amulet he held comes back to your pack, wherever he fell: never to a
 * floor, where a stash could lose it. With all 26 letters taken it goes
 * with him, and he holds it still when he rises. */
void wiz_died(void) WIZ_ENTRY
{
    wiz.mend = 0;
    gone(WIZ_RISE_MIN, WIZ_RISE_RND);
    if (wiz.deaths < 255) wiz.deaths++;
    if (wiz.amu) {
        obj_t o;
        o.otyp = O_AMULET; o.ench = 0; o.ero = 0; o.worn = 0;
        o.buc  = wiz.buc;  o.qty  = 1;
        if (item_inv_add(&o)) {
            wiz.amu = 0;
            has_amulet = 1;
            msg_more("The Amulet is yours again!");
        } else {
            msg_more("The Amulet vanishes with him!");
        }
    }
}
