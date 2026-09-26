/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* attract.c - the title screen's attract demo.
 *
 * Left alone, the title plays its theme twice (music.c) and then hands the
 * screen to this: about fifteen seconds of consecutive levels of a random
 * dungeon, each magic-mapped, walked to the down stairs by a hero of a random
 * class with his dog at heel, under a blinking "DEMO" line. Then title_screen
 * shows the art again. A key at any point begins the game, exactly as it does
 * on the title.
 *
 * IT DRAWS WITH THE REAL GAME. The levels come from build_level, the view from
 * fov_update, the pictures from draw_map and draw_status -- the game's own
 * renderer on the game's own state, not a copy of either, so the demo cannot
 * drift from what the game looks like. That is safe because the title is only
 * ever reached with no unsaved run in RAM: at boot, and after S wrote the
 * whole run to disk (load_game reads all of it back; the fresh-start path in
 * main() builds a new one).
 *
 * WHAT KEEPS IT HARMLESS is what it does not call: no try_move, no
 * monsters_turn, no upkeep. Nobody fights, picks anything up, springs a trap,
 * eats or dies, so no persistence mask, counter or conduct moves. The hero
 * walks monster_ai.c's own chase field flooded from the stairs, the dog heels
 * and every other monster ambles like a townsman. What the demo does change
 * comes in two kinds. The sheet, pack, purse, pet and fog pool are rewritten
 * by BOTH ways into a game (pick_class/item_reset/give_kit/fov_reset, or
 * load_game), so it sets them freely. The depth, its high-water mark, the turn
 * counter, the hero's facing and his blindness are not -- main()'s fresh start
 * takes them at their boot values -- so those are put back on the way out. A
 * demo that ever calls gameplay code must extend that list.
 *
 * BANKING: cold code and its one string, const-banked together (banks.json);
 * the string only ever goes to the resident print_str(). */

#include "game.h"
#include "platform.h"
#include "rng.h"
#include "level.h"
#include "monster.h"
#include "item.h"
#include "classes.h"
#include "nexthack.h"
#include "attract.h"

#define DEMO_FRAMES 750   /* a showing lasts about this long (15 s): no new */
#define LAST_START  (DEMO_FRAMES - 125)   /* level starts in its last 2.5 s,
                           * and the one under way is finished -- so a level
                           * of ~3 s lands the end near DEMO_FRAMES         */
#define MAX_LEVELS    6   /* and never more levels than this               */
#define WALK       24     /* the hero comes in WALK/2..WALK steps short of
                           * the stairs -- inside the 128K field's 30 --
                           * and half that on a level that is one great lit
                           * room (the Big Room, the cavern): there every
                           * cell is in view, and a step costs the Next's
                           * draw_map some 2.5 times a normal one */
#define F_ARRIVE   20     /* 50 Hz frames to take in a fresh level (0.4 s) */
#define F_LEAVE    12     /* standing on the stairs before the next level  */

/* The pace. The Next's timing code assumes a bare .nex runs with interrupts
 * off, so there the poll loop itself is the clock (370 polls ~ 20 ms,
 * calibrated like getkey_rpt's RPT_GUARD and the title theme's) -- and a poll
 * loop can only wait AFTER a step's own work, so the Next waits less per
 * step. The 128K counts FRAMES from t0, so there the work falls inside the
 * step's 0.1 s. Every poll stirs the seed, as the title's wait does. */
#ifdef __ZXNEXT
#define F_STEP      1
#define F_WORK      6     /* ...and the showing's clock counts a step's own
                           * work as this many frames (x3 in one great room),
                           * a level's build as F_BUILD -- as measured under
                           * ZEsarUX, which runs the Next at 14 MHz */
#define F_BUILD    20
#else
#define F_STEP      5
#endif
#define BLINK    0x20     /* the DEMO line: 32 frames lit, 32 dark (0.64 s) */

/* The keyboard has no buffer, and building a level or redrawing a big lit
 * room keeps the CPU off it for a quarter to half a second: a tap inside that
 * is gone before anyone looks. But the ROM's frame interrupt, wherever it
 * runs, scans the keys and latches each new press in FLAGS bit 5. The 128K
 * always takes it (it is what ticks FRAMES there), and FLAGS is a 48K system
 * variable, so the 48K ROM behind a RAM-expansion interface keeps it too. The
 * Next takes it under ZEsarUX (FRAMES ticks there, measured); where it does
 * not, FLAGS never changes and the direct read carries on alone. Nothing else
 * in the game reads the bit, and attract_demo clears it before it starts. */
#define FLAGS       (*(volatile uint8_t *)23611)
#define key_hit()   ((FLAGS & 0x20) || in_inkey())

static uint8_t showings;    /* how many demos the title has run (1 B resident) */

/* A showing's state, on attract_demo's stack (no resident bytes). */
typedef struct {
    uint16_t s;         /* entropy, stirred by every poll: the title's seed  */
    uint16_t start;     /* the clock when the showing began                  */
#ifdef __ZXNEXT
    uint16_t clk;       /* the Next's clock: frames waited + work estimated  */
#endif
    uint8_t  shown;     /* the DEMO line on screen: 0 dark, 1 lit, 2 unknown */
} demo_t;

/* The showing's clock, in frames. The 128K reads FRAMES itself, twice until
 * both agree: the ISR can tick it between the two bytes of a 16-bit read. */
#ifdef __ZXNEXT
#define now(d)      ((d)->clk)
#else
#define FRAMES16    (*(volatile uint16_t *)23672)
static uint16_t frames16(void)
{
    uint16_t a;
    do { a = FRAMES16; } while (a != FRAMES16);
    return a;
}
#define now(d)      frames16()
#endif

/* Light or darken the DEMO line, centred on the message row, when the clock
 * says so -- only on a change, so the line is not rewritten every poll. */
static const char demo_line[] = "DEMO - press any key to begin";

static void banner(demo_t *d)
{
    uint8_t on = (uint8_t)!(now(d) & BLINK);
    if (on == d->shown) return;
    d->shown = on;
    clear_line(0, C_WHITE);
    if (on)
        print_str((uint8_t)((TM_W - (sizeof demo_line - 1)) / 2), 0,
                  demo_line, C_WHITE | C_BRIGHT);
}

/* Wait until `frames` have passed since t0 (see above). 1 = a key. It looks
 * at the keyboard at least once even when the step's work has already used
 * up the time -- a 128K step that scrolls the view can -- or a tap in the
 * middle of a slow stretch would be lost. */
static uint8_t demo_wait(demo_t *d, uint16_t t0, uint8_t frames)
{
#ifdef __ZXNEXT
    (void)t0;
    while (frames--) {
        uint16_t guard = 370;
        while (--guard) {
            if (key_hit()) return 1;
            d->s += 0x9E37u;
        }
        d->clk++;
        banner(d);
    }
#else
    do {
        if (key_hit()) return 1;
        d->s += 0x9E37u;
        banner(d);
    } while ((uint16_t)(now(d) - t0) < frames);
#endif
    return 0;
}

static uint8_t adiff(uint8_t a, uint8_t b)
{
    return (uint8_t)(a > b ? a - b : b - a);
}

static uint8_t cheb(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by)
{
    uint8_t dx = adiff(ax, bx), dy = adiff(ay, by);
    return dx > dy ? dx : dy;
}

/* Where the hero comes in: the up stairs when they are hi/2..hi steps from
 * the down stairs, else a random room cell that is -- so every level is on
 * screen long enough to see and ends with the stairs taken. (The 128K's field
 * stops at 30 steps, which is why a far '<' cannot simply be walked from.)
 * If no probe lands, '<' it is: too near, the level is brief; off the field,
 * the hero waits it out there. */
static void pick_start(uint8_t hi)
{
    uint8_t t, x, y, d, lo = (uint8_t)(hi >> 1);
    hero_x = up_x; hero_y = up_y;
    d = dist_at(up_x, up_y);
    if ((d >= lo && d <= hi) || !rcount) return;
    for (t = 0; t < 48; t++) {
        rand_floor(rn2(rcount), &x, &y);
        d = dist_at(x, y);
        if (d < lo || d > hi) continue;
        if (lvl[y][x] != '.' || monster_at(x, y) >= 0) continue;
        hero_x = x; hero_y = y;
        return;
    }
}

/* The dog heels: into the cell the hero just left when that is one step
 * away -- which retraces his route round every corridor bend -- else to the
 * free neighbour nearest him. At heel already, it waits for him. */
static void dog_heel(uint8_t px, uint8_t py)
{
    uint8_t p, hx = (uint8_t)hero_x, hy = (uint8_t)hero_y, bx = px, by = py, bc;
    int8_t dx, dy;

    if (pet_idx < 0) return;
    p = (uint8_t)pet_idx;
    if (!m_alive[p] || cheb(m_x[p], m_y[p], hx, hy) <= 1) return;
    if (cheb(m_x[p], m_y[p], px, py) > 1 || monster_at(px, py) >= 0) {
        bc = cheb(m_x[p], m_y[p], hx, hy);
        bx = 255;
        for (dy = -1; dy <= 1; dy++)
            for (dx = -1; dx <= 1; dx++) {
                uint8_t x = (uint8_t)(m_x[p] + dx), y = (uint8_t)(m_y[p] + dy), c;
                char t;
                if (x >= MAPW || y >= MAPH) continue;   /* -1 wraps past it too */
                t = lvl[y][x];
                if (t == '|' || t == '-' || t == ' ') continue;
                if ((x == hx && y == hy) || monster_at(x, y) >= 0) continue;
                c = cheb(x, y, hx, hy);
                if (c < bc) { bc = c; bx = x; by = y; }
            }
        if (bx == 255) return;                          /* boxed in: wait */
    }
    mon_face_to(p, bx);
    m_x[p] = bx; m_y[p] = by;
}

/* One step down the field toward the stairs. Among the lower neighbours it
 * takes the one nearest the stairs as the crow flies: an open room's diagonal
 * ties would otherwise zig-zag. Whoever stands there steps into the hero's
 * cell, the way try_move lets the dog, the keeper and the townsfolk past;
 * then the dog heels. */
static void hero_walk(void)
{
    uint8_t px = (uint8_t)hero_x, py = (uint8_t)hero_y;
    uint8_t cur = dist_at(px, py), bd = cur, bm = 255, bx = 0, by = 0;
    int8_t dx, dy;
    int mi;

    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            uint8_t x = (uint8_t)(px + dx), y = (uint8_t)(py + dy), d, m;
            if (x >= MAPW || y >= MAPH) continue;       /* -1 wraps past it too */
            d = dist_at(x, y);
            if (d >= cur) continue;                      /* downhill only */
            m = (uint8_t)(adiff(x, dn_x) + adiff(y, dn_y));
            if (d < bd || (d == bd && m < bm)) { bd = d; bm = m; bx = x; by = y; }
        }
    if (bd == cur) return;                               /* no way down: wait */

    if (bx != px) hero_face = (uint8_t)(bx > px);
    mi = monster_at(bx, by);
    if (mi >= 0) {                                       /* it steps aside */
        mon_face_to((uint8_t)mi, px);
        m_x[mi] = px; m_y[mi] = py;
    }
    hero_x = bx; hero_y = by;
    dog_heel(px, py);
}

/* One level: build it, bring the hero and his dog in, walk to the stairs.
 * 1 = a key was pressed. */
static uint8_t demo_level(demo_t *d)
{
    uint8_t i, hi;
    uint16_t t0;

    /* The keyboard is also read between the slow parts -- the build, the
     * redraws. The latch (key_hit) would catch a tap there anyway; where no
     * frame interrupt runs, these reads are what narrow the blind spots. */
    build_level();
    dist_map_from(dn_x, dn_y);          /* the chase field, flooded from '>' */
    if (key_hit()) return 1;
    hi = (uint8_t)(rcount > 1 ? WALK : WALK / 2);
    pick_start(hi);
    place_pet();                        /* the dog comes in at his side */
    fov_update(hero_x, hero_y);
    fov_reveal_built();                 /* magic-mapped: all of it at once */
    draw_status();
    draw_map();
#ifdef __ZXNEXT
    d->clk += F_BUILD;
#endif
    banner(d);
    if (demo_wait(d, now(d), F_ARRIVE)) return 1;

    for (i = 0; i <= hi && dist_at((uint8_t)hero_x, (uint8_t)hero_y); i++) {
        t0 = now(d);
        hero_walk();
        monsters_amble();
        turns++;
        fov_update(hero_x, hero_y);
        if (key_hit()) return 1;
        banner(d);                      /* keep the blink even mid-step */
        draw_status();
        draw_map();
#ifdef __ZXNEXT
        d->clk += (uint8_t)(hi == WALK ? F_WORK : 3 * F_WORK);
#endif
        if (demo_wait(d, t0, F_STEP)) return 1;
    }
    return demo_wait(d, now(d), F_LEAVE);
}

uint16_t attract_demo(void) __banked
{
    demo_t   d;
    uint16_t o_dlvl = dlvl, o_max = max_dlvl, o_turns = turns, first, last;
    uint8_t  o_face = hero_face, o_blind = st_blind, lv, key = 0;

    /* A clean slate -- the resets new_game makes. At boot they change
     * nothing; after S the RAM still holds the saved run (safe on disk),
     * whose kill masks, stashes and fog would bleed into these levels. */
    item_reset();
    level_reset_persistence();
    monster_reset_persistence();
    fov_reset();
    st_blind = 0;
    FLAGS &= (uint8_t)~0x20;            /* forget presses from before the demo */

    /* a new world each showing: machine noise stirred into the last one */
    first = world_seed;
    rng_seed();
    world_seed ^= (uint16_t)(first * 0x9E37u);
    rng_set(world_seed);

    class_apply(rn2(NCLASS));           /* who walks it -- kit and all */
    give_kit();
    have_pet = 1; pet_hp = 8;
    turns = 0;

    /* Floor after floor down from a random depth that leaves room for them
     * all above the Amulet's level (which has no way down) -- and every third
     * showing the Gnomish Mines instead, down to the luckstone at their
     * bottom. A count, not a die: in an emulator every boot replays the same
     * seeds, and a 1-in-6 roll there came up Mines on the first two showings
     * every time. */
    if (++showings % 3) {
        first = (uint16_t)(1 + rn2(DLVL_AMULET - MAX_LEVELS));
        last  = DLVL_AMULET - 1;
    } else {
        first = MINES_BASE;
        last  = MINES_BASE + MINES_DEPTH - 1;
    }
    d.s = 1;
    d.shown = 2;
#ifdef __ZXNEXT
    d.clk = 0;
#endif
    tm_cls();
    d.start = now(&d);
    for (lv = 0; !key && lv < MAX_LEVELS && first + lv <= last; lv++) {
        if (lv && (uint16_t)(now(&d) - d.start) >= LAST_START) break;
        dlvl = (uint16_t)(first + lv);
        key = demo_level(&d);
    }

    dlvl = o_dlvl; max_dlvl = o_max; turns = o_turns;
    hero_face = o_face; st_blind = o_blind;
    return key ? (d.s ? d.s : 1) : 0;
}
