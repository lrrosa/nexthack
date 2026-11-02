/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* ascend.c - BANKED: the end of the game (2.0). Climbing out of Dlvl 1 with
 * the Amulet no longer wins; it leads to the Astral Plane (DLVL_ASTRAL), and
 * the game is won by offering the Amulet on the altar of your own god.
 *
 * The plane is a hand-drawn map (tools/astral.txt, packed by
 * tools/txt2template.py into astral_data.h, const-banked beside this code)
 * that lz_expand -- resident -- unpacks straight into lvl[][], as the special
 * templates do. It is kept out of the templates' random pool, so no existing
 * world changes. Three temples stand along the top, an altar in each, one
 * per god in an order each world shuffles (altar_align, level.c); from afar
 * an altar is just an altar, and only up close does it show whose it is
 * (farlook.c). A high priest prays above each: yours is at peace with you,
 * the other two are not, and all three sleep until you come near.
 *
 * Everything new lives here, and the fuller banks it hooks into carry one
 * call each: gen_level, spawn_level_monsters and do_drop. */

#include "ascend.h"
#include "game.h"
#include "level.h"        /* lvl, rcount, up_x..dn_y, rand_floor, altar_align */
#include "monster.h"
#include "platform.h"     /* lz_expand, msg, msg_more */
#include "rng.h"          /* rn2 */
#include "sfx.h"
#include "nexthack.h"     /* draw_map */

extern uint8_t r_x[], r_y[], r_w[], r_h[];   /* room rects (levelgen.c) */

#include "astral_data.h"  /* astral_lz, astral_room, astral_altar, ASTRAL_UP_X/Y */

#define FIELD (ASTRAL_NROOM - 1)    /* the open plane below the temples */

/* gen_level's special path: the map, its rooms (so FOV lights the temples
 * and the field) and the arrival cell. No loot and no stairs: the plane's
 * only way on is the altar. dn_x/dn_y name the arrival too, so nothing that
 * reads a level's way down finds a stale one. */
void astral_stamp(void) __banked
{
    uint8_t r;
    lz_expand(astral_lz, &lvl[0][0], (uint16_t)MAPW * MAPH);
    up_x = dn_x = ASTRAL_UP_X;
    up_y = dn_y = ASTRAL_UP_Y;
    rcount = ASTRAL_NROOM;
    for (r = 0; r < ASTRAL_NROOM; r++) {
        r_x[r] = astral_room[r][0]; r_y[r] = astral_room[r][1];
        r_w[r] = astral_room[r][2]; r_h[r] = astral_room[r][3];
    }
}

/* spawn_level_monsters hands over here once it has cleared the slots: instead
 * of its rooms-at-random, a priest in each temple and the plane's own in the
 * field only (the random spawner would put three in four of them in the
 * temples). Drawn from the plane's reseeded stream, so every visit -- and
 * every restore -- sees the same crowd. The plane is past MAXLVL, where no
 * kill mask has a slot (game.h), so m_track names nobody: what dies here
 * comes back after a restore, as does a priest you angered. */
void astral_populate(void) __banked
{
    const MonType *mt = mon_find('M');
    uint8_t i, n, x, y;
    char t;

    m_track = 0;
    for (i = 0; i < 3; i++) {
        x = astral_altar[i][0]; y = astral_altar[i][1];
        m_x[i] = x; m_y[i] = (uint8_t)(y - 1);
        m_hp[i] = mt->hp;
        m_type[i] = 'M';
        m_alive[i] = 1;
        m_sleep[i] = 255;               /* at prayer until you come near */
        m_peace[i] = (uint8_t)(altar_align(x, y) == alignment);
    }
    mcount = 3;
    /* eight in all, as on any deep level (spawn_level_monsters' cap), which
     * leaves the pet its slot and one more for a follower or a wanderer */
    for (n = 0; n < 16 && mcount < 8; n++) {
        t = pick_mon();
        if (mon_gone(t)) continue;
        rand_floor(FIELD, &x, &y);
        if (lvl[y][x] != '.' || (x == up_x && y == up_y) || monster_at(x, y) >= 0)
            continue;
        m_x[mcount] = x; m_y[mcount] = y;
        m_hp[mcount] = (uint8_t)(mon_find(t)->hp + eff_depth() / 2);
        m_type[mcount] = t;
        m_alive[mcount] = 1;
        if (rn2(2)) m_sleep[mcount] = 255;
        mcount++;
    }
}

/* `d` on the Amulet. Anywhere but an altar it stays in the pack, as it always
 * did; on an ordinary altar the god is far away. On the plane, your own god
 * takes it and the game is won -- turnless, like the old winning climb: no
 * monster gets a last blow. Another god strikes you down to a quarter of
 * your hit points and gives it back: the bolt alone never kills, but it costs
 * the turn, and its priest may be standing beside you. */
void offer_amulet(void) __banked
{
    if (terrain(hero_x, hero_y) != '_') {
        msg("You dare not part with it!");
        return;
    }
    if (dlvl != DLVL_ASTRAL) {
        msg("You feel the surface calling.");
        return;
    }
    cnt_prayers++;                      /* an offering is worship (conducts) */
    draw_status();                      /* the drop list off, the plane back */
    draw_map();                         /* (the 128K's list took the status rows) */
    sfx_magic();
    if (altar_align((uint8_t)hero_x, (uint8_t)hero_y) == alignment) {
        msg_more("An invisible choir sings...");
        msg_more("You are bathed in radiance!");
        msg_more("You ascend to demigodhood!");
        won = 1;
        return;
    }
    acted = 1; turns++;
    msg_more("\"Thou art not my servant!\"");
    php = (uint8_t)(php / 4u);
    if (php == 0) php = 1;
    sfx_hurt();
    msg("A bolt of lightning strikes you!");
}
