/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* throne.c - '#': sitting down (1.8), and the throne, which is why anyone
 * would. NetHack's dosit: two sittings in three nothing happens; otherwise one
 * of thirteen things, good and bad, and after every sitting a third of the
 * thrones vanish. Its own module because its thirteen lines of text would not
 * fit beside the rest of the furniture in one of the 128K's banks.
 *
 * Two of NetHack's thirteen are changed, by the rule this game keeps -- an
 * effect needs something here to work on: the wish (decided against: it would
 * be the strongest thing in the game, and needs a chooser of its own) is the
 * luck it falls back to when the luck is bad, every time; and see invisible,
 * which nothing here needs, is the magic mapping it falls back to. */

#include "level.h"
#include "game.h"
#include "platform.h"
#include "rng.h"
#include "monster.h"
#include "item.h"
#include "item_int.h"
#include "nexthack.h"     /* hero_teleport */
#include "sfx.h"

/* NetHack's courtmon: the deeper, the grander the court */
static char courtier(void)
{
    uint16_t d3 = (uint16_t)(3u * eff_depth());
    uint8_t i = (uint8_t)(rn2(60) + rn2((uint8_t)(d3 > 250 ? 250 : d3 ? d3 : 1)));
    if (i > 100) return 'D';
    if (i > 85)  return 'T';
    if (i > 60)  return 'o';
    if (i > 45)  return 'h';
    if (i > 15)  return 'G';
    return 'k';
}

static void lose_attr(void)
{
    static uint8_t *const at[6] = { &at_str, &at_dex, &at_con,
                                    &at_int, &at_wis, &at_cha };
    static const char *const feel[6] = {
        "You feel weak!", "You feel clumsy!", "You feel fragile!",
        "You feel stupid!", "You feel foolish!", "You feel repulsive!"
    };
    uint8_t k = (uint8_t)rn2(6), d = (uint8_t)(rn2(4) + 3), *a = at[k];
    *a = (uint8_t)(*a > d + 3 ? *a - d : 3);
    msg(feel[k]);
}

static void sit_throne(void)
{
    uint8_t i, n;
    if (rn2(6) < 4) {
        msg("You feel somehow out of place...");
    } else switch (rn2(13)) {
    case 0:                                     /* a cursed throne */
        lose_attr();
        n = (uint8_t)(rn2(10) + 1);
        if (php <= n) { php = 0; dead = 1; } else php = (uint8_t)(php - n);
        break;
    case 1:
        use_gain_ability();
        break;
    case 2:
        msg("A massive shock jolts you!");
        sfx_hurt();
        n = (uint8_t)(rn2(30) + 1);
        if (php <= n) { php = 0; dead = 1; } else php = (uint8_t)(php - n);
        break;
    case 3:
        php = pmaxhp;
        st_conf = 0; st_poison = 0;
        if (st_blind && !blindfolded) { st_blind = 0; map_dirty = 1; }
        msg("You feel much, much better!");
        break;
    case 4:
        msg(gold ? "You notice you have no gold!" : "You feel a strange sensation.");
        gold = 0;
        break;
    case 5:                                     /* NetHack's wish, refused */
        if (luck < 5) luck++;
        msg("You feel your luck is changing.");
        break;
    case 6:
        msg("Thy audience hath been summoned!");
        for (n = (uint8_t)(rn2(10) + 1); n; n--) summon_near(courtier(), 1);
        break;
    case 7:
        msg_more("By thine Imperious order...");
        use_genocide();
        break;
    case 8:
        msg_more("A curse upon thee for sitting!");
        if (luck > 0) {
            ADD_SAT8(st_blind, 250);
            map_dirty = 1;
            luck = (int8_t)(luck - (luck > 1 ? 1 : 2));
            msg("Darkness falls around you.");
        } else {
            for (n = (uint8_t)(rn2(3) + 1); n && inv_count; n--) {
                i = (uint8_t)rn2(inv_count);
                if (inv[i].otyp != O_AMULET)
                    inv[i].buc = (uint8_t)((inv[i].buc & BUC_KNOWN) | BUC_CURSE);
            }
            /* a cursed piece you have on is worth 2 less (gear_eff): the
             * damage and the AC change now, not at the next thing that
             * recomputes them (#22) */
            item_recompute_gear();
            msg("You feel you need some help.");
        }
        break;
    case 9:
        fov_reveal();
        map_flush = 1;
        msg("An image forms in your mind.");
        break;
    case 10:
        if (luck < 0) {
            for (i = 0; i < mcount; i++) if (m_alive[i]) m_sleep[i] = 0;
            msg("You feel threatened.");
        } else {
            msg("You feel a wrenching sensation.");
            hero_teleport();
        }
        break;
    case 11:
        for (i = 0; i < inv_count; i++) {
            item_id_set(inv[i].otyp);
            inv[i].buc |= BUC_KNOWN;
        }
        msg("You are granted an insight.");
        break;
    default:
        ADD_SAT8(st_conf, rn2(7) + 16);
        msg("Your mind turns into a pretzel!");
        break;
    }
}

void do_sit(void) __banked
{
    uint8_t x, y;
    acted = 1; turns++;
    switch (lvl[hero_y][hero_x]) {
    case CH_THRONE:
        sit_throne();
        /* after every sitting a third of the thrones go -- the one under
         * you, as NetHack checks it: a teleport may have taken you off it */
        x = (uint8_t)hero_x; y = (uint8_t)hero_y;
        if (!dead && lvl[y][x] == CH_THRONE && rn2(3) == 0) {
            msg_more(0);
            lvl[y][x] = '.';
            dug_add(x, (uint8_t)(y | DUG_FLOOR));
            map_flush = 1;
            msg("The throne vanishes in a puff.");
        }
        return;
    case CH_SINK:   msg("You sit on the sink.");          return;
    case CH_GRAVE:  msg("You sit on the grave.");         return;
    case '_':       msg("You sit on the altar.");         return;
    case '{':       msg("You sit on the fountain's rim."); return;
    case '<': case '>': msg("You sit on the stairs.");    return;
    default:        msg("Having fun sitting on the floor?"); return;
    }
}
