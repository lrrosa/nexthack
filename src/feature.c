/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* feature.c - the dungeon's furniture (1.8): where sinks, thrones and graves
 * stand, and what sinks and graves do. Sitting on a throne is throne.c.
 *
 * WHERE. Like the altar and the fountain (nexthack_lvl.c), a side hash of the
 * world and the depth -- never rn2, so generation and every persistence index
 * are untouched -- picks the level and the room, and the piece goes on a fixed
 * interior cell of it only if that cell is plain floor outside a shop. It runs
 * beside them in build_level, after the spawns and BEFORE the persistence, for
 * their reason: a pile the hero took is floor on the next visit, and a piece
 * decided after it would grow there. Ordinary levels only: the hand-drawn
 * maps, the Big Room and the mines keep their own furniture.
 *
 * WHAT CHANGES FOR GOOD rides the dug pool (levelfov.c), as a dried fountain
 * does: a throne that vanished and a grave dug up are floor (DUG_FLOOR), and
 * a sink that has given up its ring carries a mark (DUG_MARK) that puts
 * nothing back but is found by dug_marked.
 *
 * item_int.h is included for inv[] and the O_* ids, which the sink's ring
 * needs -- the third module to use it after item.c and item_use.c, under the
 * same rule: only values cross, never a pointer into item.c's catalogue. */

#include "level.h"
#include "game.h"
#include "platform.h"
#include "rng.h"
#include "monster.h"
#include "item.h"
#include "item_int.h"
#include "sfx.h"

extern uint8_t r_x[], r_y[], r_w[], r_h[];   /* levelgen.c */

static void put_piece(uint8_t x, uint8_t y, char c)
{
    if (lvl[y][x] == '.' && !shop_in_room(x, y)) lvl[y][x] = c;
}

void place_features(void) __banked
{
    uint16_t h;
    uint8_t r, d = (uint8_t)eff_depth();

    if (rcount == 0 || IN_MINES(dlvl) || level_is_special()) return;
    /* a sink, one level in seven, in a room's top-right corner -- NetHack
     * puts one in a room in sixty, at any depth */
    h = (uint16_t)(world_seed * 5u + (uint16_t)dlvl * 0x3B1Du);
    if ((h % 7u) == 0) {
        r = (uint8_t)((h >> 3) % rcount);
        put_piece((uint8_t)(r_x[r] + r_w[r] - 2), (uint8_t)(r_y[r] + 1), CH_SINK);
    }
    /* a throne, one level in ten from Dlvl 5 (NetHack's throne rooms start
     * below 4), against the middle of a room's top wall */
    h = (uint16_t)(world_seed * 7u + (uint16_t)dlvl * 0x5A27u);
    if (d >= 5 && (h % 10u) == 0) {
        r = (uint8_t)((h >> 3) % rcount);
        put_piece((uint8_t)(r_x[r] + r_w[r] / 2), (uint8_t)(r_y[r] + 1), CH_THRONE);
    }
    /* a grave, likelier the deeper you go, as NetHack's (a room in 80 - 2 *
     * depth): one level in 15 at Dlvl 2, one in 4 from Dlvl 48, in a
     * room's bottom-left corner */
    h = (uint16_t)(world_seed * 11u + (uint16_t)dlvl * 0x6F35u);
    if (d >= 2 && (h % (uint8_t)(d >= 48 ? 4 : 16 - d / 4)) == 0) {
        r = (uint8_t)((h >> 3) % rcount);
        put_piece((uint8_t)(r_x[r] + 1), (uint8_t)(r_y[r] + r_h[r] - 2), CH_GRAVE);
    }
}

/* ---- sinks ---------------------------------------------------------------- */

static void hurt(uint8_t d)
{
    if (php <= d) { php = 0; dead = 1; }
    else          php = (uint8_t)(php - d);
}

/* The ring a sink holds, once: dug_marked remembers it is gone. */
static uint8_t sink_ring_out(uint8_t x, uint8_t y)
{
    if (dug_marked(x, y)) return 0;
    item_spawn(x, y, '=', 0);
    dug_add(x, (uint8_t)(y | DUG_MARK));
    map_flush = 1;
    return 1;
}

/* 'q' on a sink: NetHack's drinksink, the parts this dungeon has the
 * creatures and the plumbing for. */
void quaff_sink(void) __banked
{
    uint8_t x = (uint8_t)hero_x, y = (uint8_t)hero_y;
    acted = 1; turns++;
    switch (rn2(20)) {
    case 0: msg("You sip very cold water."); break;
    case 1: msg("You sip very warm water."); break;
    case 2: msg("You sip scalding hot water!"); hurt((uint8_t)(rn2(6) + 1)); break;
    case 3:
        msg(summon_near('r', 1) ? "Eek!  A rat in the sink!" : "The sink seems quite dirty.");
        break;
    case 4:
        msg(sink_ring_out(x, y) ? "You find a ring in the sink!"
                                : "Dirty water backs up the drain.");
        break;
    case 5:
        msg("Gaggg... it tastes of sewage!");
        nutrition = (int16_t)(nutrition - 40);   /* NetHack: you vomit */
        break;
    case 6: msg("Yuk, this water tastes awful."); break;
    case 7: msg("Clanking echoes from the pipes."); break;
    case 8: msg("Snatches of song from the sewer."); break;
    default: msg(rn2(3) ? "You take a sip of cold water."
                        : "You take a sip of hot water."); break;
    }
}

/* 'K' at a sink: mostly it rattles; once in a while the drain gives up
 * what it was hiding -- a ring, or something that lived down there. */
void kick_sink(uint8_t x, uint8_t y) __banked
{
    acted = 1; turns++;
    sfx_hit();
    if (rn2(5) || dug_marked(x, y)) { msg("Klunk!  The pipes vibrate."); return; }
    if (rn2(2)) {
        sink_ring_out(x, y);
        msg("Flupp!  Out pops a ring!");
    } else {
        dug_add(x, (uint8_t)(y | DUG_MARK));
        msg("Flupp!  Muddy waste pops up!");
        summon_near('a', 1);
    }
}

/* A ring dropped down a sink ('d' on one, item.c do_drop): NetHack's way to
 * learn a ring for the price of the ring. Each kind does something you can
 * see, which names it -- all but hunger, which only eats what lies on the
 * sink, and nothing does. Slow digestion comes back up, and one time in
 * twenty the sink backs up and returns any of them. A ring that comes back
 * is still the hero's: on a floor too full to take it (MAXFLOOR things and
 * no corpse to give way) it goes back in the pack -- which has the room it
 * just left -- rather than vanish under a message saying it came back. */
void sink_ring(uint8_t s) __banked
{
    obj_t o = inv[s];
    uint8_t x = (uint8_t)hero_x, y = (uint8_t)hero_y, back = 0, learn = 1;
    const char *m;
    acted = 1; turns++;
    item_inv_remove(s);
    item_recompute_gear();
    o.worn = 0;
    switch (o.otyp) {
    case O_PROTECT:   m = o.ench < 0 ? "The sink glows black a moment."
                                     : "The sink glows silver a moment."; break;
    case O_REGEN:     m = "The sink looks as good as new.";    break;
    case O_RSLOWDIG:  m = "The ring is regurgitated!"; back = 1; break;
    case O_RFREEACT:  m = "The ring slides down the drain!";   break;
    case O_RTCTRL:    m = "The sink seems to be beamed up.";   break;
    case O_RSTEALTH:  m = "The sink fades into the shadows.";  break;
    case O_RAGGR:     m = "Flies buzz angrily at the sink!";   break;
    case O_RTPORT:
        m = "The sink vanishes.";
        lvl[y][x] = '.';
        dug_add(x, (uint8_t)(y | DUG_FLOOR));
        map_flush = 1;
        break;
    default:          m = "The ring bounces down the drain."; learn = 0; break;
    }
    if (learn) item_id_set(o.otyp);
    if (!back && rn2(20) == 0) {
        msg_more(m);
        m = "The sink backs up, leaving it.";
        back = 1;
    }
    if (back && !item_floor_place(x, y, &o)) {
        msg_more(m);
        m = "You fish it back out.";
        item_inv_add(&o);
    }
    msg(m);
}

/* ---- graves --------------------------------------------------------------- */

static uint8_t grave_hash(uint8_t x, uint8_t y)
{
    uint16_t h = (uint16_t)(world_seed * 19u + (uint16_t)dlvl * 233u
                            + (uint16_t)x * 7u + (uint16_t)y * 101u);
    h ^= (uint16_t)(h >> 7);
    return (uint8_t)h;
}

/* What the headstone says -- on stepping there (describe) and on a look. */
void grave_say(uint8_t x, uint8_t y) __banked
{
    static const char *const ep[8] = {
        "Rest in peace.", "Should have prayed.", "One kick too many.",
        "It was a mimic.", "Read it unidentified.", "Petted a dragon.",
        "Hit the floating eye.", "Forgot to eat."
    };
    msg2("A grave: \"", ep[grave_hash(x, y) & 7], "\"");
}

/* Digging a grave up -- the pick-axe's five turns down, or a wand of digging
 * zapped down -- NetHack's dig_up_grave: a lawful hero has done wrong, and the
 * grave gives up a corpse, an angry owner, or nothing; some held a cursed
 * keepsake and a little gold besides, as NetHack's mkgrave buries them. */
void grave_dig(void) __banked
{
    uint8_t x = (uint8_t)hero_x, y = (uint8_t)hero_y, k;
    obj_t o;
    lvl[y][x] = '.';
    dug_add(x, (uint8_t)(y | DUG_FLOOR));
    map_flush = 1;
    if (alignment == 0) {
        if (luck > -5) luck--;
        msg_more("You desecrate this grave!");
    }
    if (rn2(3) == 0) item_spawn(x, y, 0, 1);
    if (rn2(3) == 0) {
        uint16_t g = (uint16_t)(rn2(20) + 1 + eff_depth() * 2);
        gold = (gold > (uint16_t)(60000u - g)) ? 60000u : (uint16_t)(gold + g);
        msg_more("Some gold was buried here.");
    }
    switch (k = rn2(5)) {
    case 0: case 1:
        o.otyp = O_CORPSE; o.ench = (int8_t)pick_mon(); o.ero = 0; o.worn = 0; o.buc = 0; o.qty = 1;
        item_floor_place(x, y, &o);
        msg("You unearth a corpse.");
        break;
    case 2: case 3:
        msg(k == 2 ? "The grave's owner is very upset!" : "You've disturbed a tomb!");
        summon_near((k == 3 && eff_depth() >= 8) ? 'W' : 'Z', 1);
        break;
    default:
        msg("The grave seems unused. Strange.");
        break;
    }
}
