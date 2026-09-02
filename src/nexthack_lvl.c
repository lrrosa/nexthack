/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* nexthack_lvl.c - the COLD level half of nexthack.c: assembling a level, its
 * furniture and its escorts, and walking between levels.
 *
 * WHY IT IS ITS OWN FILE. nexthack.c, classes.c and spells.c form one
 * INDIVISIBLE colocate group -- classes/spells hand their const tables and
 * literals to draw_status, show_help and score_screen, so a pointer into an
 * unmapped bank would read garbage and all three must share one. That unit had
 * grown to 16358 B of the 128K's 16384 B BANK_3: 26 free bytes, and no packing
 * could help it, because it already sat alone in its bank (tools/bankpack.py
 * says exactly that). Relocation was out of levers; splitting the module was
 * the only one left.
 *
 * WHY THIS CUT. Everything here runs ONCE on level entry -- never per turn --
 * so the trampoline hop it now pays is nothing beside generating a level. And
 * none of it reads another bank's const: the only string it does not own is
 * mon_name()'s, which comes from the RESIDENT monster.c, and its own literals
 * go only to msg/msg2 in the resident platform.c. Both are the rule that makes
 * a split safe (see the bank-budget skill).
 *
 * traps_reset() is the one thing left behind: the sprung-trap set belongs to
 * the trap code in nexthack.c, so it became __banked rather than dragging the
 * traps along. It is five bytes called once per level. */

#include "game.h"
#include "platform.h"
#include "rng.h"
#include "level.h"
#include "monster.h"
#include "item.h"
#include "sfx.h"
#include "nexthack.h"

/* ============================================================
 * Level orchestration
 * ============================================================ */

/* Build the current dlvl: terrain + gold, then monsters (which must see the
 * freshly placed gold), then re-apply remembered mutations. The order keeps
 * generation deterministic across revisits. */
/* room rects (defined in levelgen.c) -- read here to drop an altar in one */
extern uint8_t r_x[], r_y[], r_w[], r_h[];

/* Neither an altar nor a fountain belongs on a shop's floor: the room is the
 * keeper's stock, and NetHack keeps temples and shops apart. It is not just
 * scenery either -- inside a shop `d` SELLS (do_drop branches on
 * shop_in_room), so drop_at_feet never runs and the altar's whole purpose
 * (sacrifice, the BUC flash, blessing a potion) is unreachable there. Both
 * placements run after gen_level and this test is a pure rect check, so
 * skipping costs no rn2 and cannot desync the deterministic generation. */
static int spot_taken(uint8_t cx, uint8_t cy)
{
    return lvl[cy][cx] != '.' || shop_in_room(cx, cy);
}

/* Some levels hold an altar. A side hash of (world_seed, dlvl) -- never rn2, so
 * the deterministic per-depth persistence stays in sync -- picks roughly one
 * level in five and one of its rooms; we drop a '_' on that room's centre, but
 * only when it is plain floor outside any shop (so it never buries stairs, a
 * door or an item, nor lands among the stock). If the pick is unusable the
 * level simply has no altar -- placement is opportunistic by design.
 * Pure terrain, regenerated identically on every visit, so nothing to save. */
static void place_altar(void)
{
    uint16_t h;
    uint8_t room, cx, cy;

    if (rcount == 0) return;
    h = (uint16_t)(world_seed + (uint16_t)dlvl * 0x9E37u);
    if ((h % 5u) != 0) return;
    room = (uint8_t)((h >> 3) % rcount);
    cx = (uint8_t)(r_x[room] + r_w[room] / 2);
    cy = (uint8_t)(r_y[room] + r_h[room] / 2);
    if (!spot_taken(cx, cy)) lvl[cy][cx] = '_';
}

/* Some levels have a fountain, chosen the same rn2-free way as the altar (a
 * different hash constant, so the two rarely land together and never overwrite
 * -- both go through spot_taken). Roughly one level in four from depth 2 down. */
static void place_fountain(void)
{
    uint16_t h;
    uint8_t room, cx, cy;

    if (rcount == 0 || eff_depth() < 2) return;
    h = (uint16_t)(world_seed * 3u + (uint16_t)dlvl * 0x2C9Fu);
    if ((h % 4u) != 0) return;
    room = (uint8_t)((h >> 4) % rcount);
    cx = (uint8_t)(r_x[room] + r_w[room] / 2);
    cy = (uint8_t)(r_y[room] + r_h[room] / 2);
    if (!spot_taken(cx, cy)) lvl[cy][cx] = '{';
}


/* (Re)place the pet next to the hero. Called after the hero's position is set
 * on every level entry (new game, descend/ascend, trap-door fall, restore), so
 * the dog always tags along. It takes the tail monster slot (after the random
 * mobs and any keeper), which the uint8_t mon_dead bitmask never tracks, and is
 * never persisted -- pet_idx/pet_hp carry it instead. Adjacent cells first; if
 * they are all blocked (a cramped stairs room whose one other floor cell holds
 * a spawned monster), the radius-2 ring is tried before giving up, so the dog
 * only sits a level out when your whole arrival neighbourhood is packed.
 * (Lives here, not monster_ai.c, to keep that bank under 16 KB.) */
void place_pet(void) __banked
{
    int dx, dy, r;
    pet_idx = -1;
    if (!have_pet || mcount >= MAXMON) return;
    for (r = 1; r <= 2; r++)
        for (dy = -r; dy <= r; dy++)
            for (dx = -r; dx <= r; dx++) {
                int x = hero_x + dx, y = hero_y + dy;
                char c;
                if (dx > -r && dx < r && dy > -r && dy < r)
                    continue;                 /* ring r only: inner cells done */
                if (pet_idx >= 0) continue;
                if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) continue;
                c = lvl[y][x];
                if (c == '|' || c == '-' || c == ' ') continue;   /* need walkable floor */
                if (monster_at(x, y) >= 0) continue;
                m_x[mcount] = (uint8_t)x; m_y[mcount] = (uint8_t)y;
                m_hp[mcount] = pet_hp;
                m_type[mcount] = 'd';
                m_alive[mcount] = 1;
                pet_idx = (int8_t)mcount;
                mcount++;
            }
}

/* ---- stair followers ----
 * An awake, hostile monster standing next to you when you take the stairs
 * comes along, as in NetHack: grabbed before the level switch, re-placed at
 * your side after it. Transient by design (never saved) -- it simply IS the
 * monster at your heel this instant. The shopkeeper keeps his shop, a posing
 * mimic holds its pose, the floating eye floats where it is, sleepers sleep
 * on and the peaceful stay home. Like the wanderers, a follower shares the
 * mon_dead bitmask space of the level it arrives on. */
static char    follow_type;
static uint8_t follow_hp;

static int iabs8(int v) { return v < 0 ? -v : v; }

static void grab_follower(void)
{
    uint8_t i;
    follow_type = 0;
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i] || i == pet_idx) continue;
        if (m_type[i] == MON_KEEPER || m_type[i] == 'x' ||
            m_type[i] == 'e') continue;
        if (m_sleep[i] || m_peace[i]) continue;
        if (iabs8((int)m_x[i] - hero_x) <= 1 &&
            iabs8((int)m_y[i] - hero_y) <= 1) {
            follow_type = m_type[i];
            follow_hp   = m_hp[i];      /* it arrives as wounded as it left */
            return;
        }
    }
}

/* Place the grabbed follower on a free cell beside the hero -- called after
 * place_pet so the dog gets first pick of the floor, and after the stairs
 * message so "The X follows you!" is the line that stays up. */
static void place_follower(void)
{
    int dx, dy;
    char t = follow_type;
    follow_type = 0;
    if (!t || mcount >= MAXMON) return;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int x = hero_x + dx, y = hero_y + dy;
            char c;
            if (dx == 0 && dy == 0) continue;
            if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) continue;
            c = lvl[y][x];
            if (c == '|' || c == '-' || c == ' ') continue;
            if (monster_at(x, y) >= 0) continue;
            m_x[mcount] = (uint8_t)x; m_y[mcount] = (uint8_t)y;
            m_hp[mcount]    = follow_hp;
            m_type[mcount]  = t;
            m_alive[mcount] = 1;
            m_sleep[mcount] = 0;
            m_peace[mcount] = 0;
            mcount++;
            msg2("The ", mon_name(t), " follows you!");
            return;
        }
}

void build_level(void) __banked
{
    if (dlvl > max_dlvl && !IN_MINES(dlvl))
        max_dlvl = dlvl;                    /* deepest point, for the score */
    el_life = 0;             /* a dust engraving does not survive a level change */
    traps_reset();           /* sprung-trap set is per visit (level regenerates) */
    floor_reset();           /* loose thrown items don't survive a level change  */
    gen_level();
    spawn_level_monsters();
    { uint8_t kx, ky; if (shop_keeper_xy(&kx, &ky)) place_shopkeeper(kx, ky); }
    apply_gold_persistence();
    apply_monster_persistence();
    apply_item_persistence();
    place_altar();       /* a deterministic altar on some levels (no RNG) */
    place_fountain();    /* ...and a fountain on some (guards on '.', so it
                          * never overwrites the altar) */
    floor_restore();     /* re-lay this level's dropped-item stash (item.c) */
    map_dirty = 1;       /* +zx: next draw_map recenters (no-op on Next) */
    /* note: FOV memory is per depth and persists across visits, so it is NOT
     * reset here - only on a new game (see new_game / main). */
}

void go_down(void) __banked
{
    if (terrain(hero_x, hero_y) == 'v') {   /* into the Gnomish Mines */
        grab_follower();
        dlvl = MINES_BASE;
        turns++;
        build_level();
        hero_x = up_x; hero_y = up_y;
        place_pet();
        msg("You descend into the mines.");
        sfx_stairs();
        place_follower();
        return;
    }
    if (terrain(hero_x, hero_y) == '>') {
        grab_follower();
        dlvl++;
        turns++;
        build_level();
        hero_x = up_x; hero_y = up_y;     /* arrive on the new up-stairs */
        place_pet();                      /* the dog follows you down */
        if (dlvl == DLVL_AMULET && !has_amulet)
            msg("A terrible presence dwells here.");   /* the Amulet's keeper */
        else
            msg("You descend the stairs.");
        sfx_stairs();
        place_follower();
    } else {
        msg("You can't go down here.");
    }
}

void go_up(void) __banked
{
    if (terrain(hero_x, hero_y) == '<') {
        if (dlvl == MINES_BASE) {           /* out of the mines */
            grab_follower();
            dlvl = MINES_ENTR_DLVL;
            turns++;
            build_level();
            hero_x = mn_x; hero_y = mn_y;   /* you emerge from the hole */
            place_pet();
            msg("You climb out of the mines.");
            sfx_stairs();
            place_follower();
        } else if (dlvl > 1) {
            grab_follower();
            dlvl--;
            turns++;
            build_level();
            hero_x = dn_x; hero_y = dn_y; /* arrive on the new down-stairs */
            place_pet();                  /* the dog follows you up */
            msg("You climb up the stairs.");
            sfx_stairs();
            place_follower();
        } else if (has_amulet) {
            won = 1;                      /* surfaced with the Amulet: victory */
        } else {
            msg("You can't go up from here.");
        }
    } else {
        msg("You can't go up here.");
    }
}
