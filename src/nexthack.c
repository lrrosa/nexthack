/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* ============================================================
 * NextHack - a roguelike for the ZX Spectrum Next
 * ------------------------------------------------------------
 * This module is the game itself: shared player/run state, the main
 * loop, rendering of the map and status bar, and the title screen.
 *
 * The codebase is split into modules:
 *   platform.c  - ZX Next hardware (tilemap, tiles, text, keyboard)
 *   rng.c       - random number generator
 *   level.c     - terrain, procedural generation, persistence
 *   monster.c   - monsters, chase AI, combat
 *   nexthack.c  - this file: game state, loop, rendering, title
 *
 * A fresh engine inspired by NetHack's design, not a recompile of
 * NetHack's C — sized for the Z80N.
 * ============================================================ */

#include "game.h"
#include "platform.h"
#include "rng.h"
#include "level.h"
#include "monster.h"
#include "item.h"
#include "sfx.h"
#include "classes.h"
#ifndef __ZXNEXT
#include "scr.h"
#endif
#include "nexthack.h"

/* ---- shared game/run state (declared extern in game.h) ---- */
int      hero_x, hero_y;
uint16_t dlvl = 1;
uint16_t turns = 0;
uint8_t  php = 12, pmaxhp = 12;
uint16_t gold = 0;
uint8_t  dead = 0;
uint8_t  has_amulet = 0;
uint8_t  luckstone_taken = 0;    /* the mines' prize was claimed (see game.h) */
uint8_t  won = 0;
uint8_t  acted = 0;
uint8_t  map_dirty = 1;   /* +zx renderer flag (unused on Next) */
uint8_t  map_flush = 0;   /* +zx: skip draw_map's fast path once (a cell changed
                           * at a distance: a throw landed, search revealed a
                           * trap, a mapping scroll) WITHOUT recentring the view */
uint8_t  weapon_dmg = 0;
uint8_t  armor_def = 0;
uint8_t  ac = 10;
uint8_t  regen_ring = 0;     /* worn ring of regeneration (see game.h) */
uint16_t xp = 0;
uint8_t  xlvl = 1;
int16_t  nutrition = 900;

/* the character sheet (see game.h). Defaults = the Tourist, matching the old
 * fixed status line; the class picker overwrites these at new_game. */
uint8_t  at_str = 14, at_dex = 11, at_con = 14;
uint8_t  at_int = 10, at_wis = 8,  at_cha = 10;
uint8_t  pclass = 0;
uint8_t  intrinsics = 0;
uint8_t  pw = 2, pmaxpw = 2;
uint8_t  known_spells = 0;
uint16_t max_dlvl = 1;
uint8_t  alignment = 0;      /* 0 Lawful / 1 Neutral / 2 Chaotic (set by class) */
int8_t   luck = 0;           /* hidden fortune, -5..+5 (see game.h) */

/* transient status effects (see game.h): per-turn countdowns, 0 = inactive */
uint8_t  st_conf = 0, st_blind = 0, st_sleep = 0, st_poison = 0;
uint8_t  el_x = 0, el_y = 0, el_life = 0;    /* Elbereth engraving (see game.h) */
uint16_t pray_timeout = 0;                   /* turns until you may pray again */
uint8_t  have_pet = 0;                        /* a living dog follows you (see game.h) */
uint8_t  pet_hp = 0;                          /* the pet's health, carried across levels */
uint8_t  pet_kills = 0;                       /* its lifetime kills (growth; see game.h) */
uint16_t cnt_kills = 0, cnt_corpses = 0, cnt_reads = 0, cnt_prayers = 0;   /* conducts */

/* hunger/regeneration bookkeeping */
static uint8_t heal_timer = 0;
static uint8_t pw_timer = 0;   /* spell power regeneration (see upkeep) */
static uint8_t hunger_state = 0;   /* 0 ok  1 hungry  2 weak  3 fainting */

/* ============================================================
 * Save / restore (NetHack-style: save & quit, consumed on load)
 * ============================================================ */

#define SAVE_NAME  "nexthack.sav"
#define SAVE_MAGIC 0x484Eu          /* 'N','H' */
#define SAVE_VER   27     /* v0.10.0: MAXINV 24->26 (INV_BYTES) and the
                           * fog-of-war pool grew to 12 slots */

struct save_hdr {
    uint16_t magic;
    uint8_t  ver;
};

struct save_player {
    uint16_t world_seed;
    int16_t  hero_x, hero_y;
    uint16_t dlvl, turns;
    uint8_t  php, pmaxhp;
    uint16_t gold;
    int16_t  nutrition;
    uint16_t xp;
    uint8_t  xlvl;
    uint8_t  has_amulet;
    uint8_t  st_conf, st_blind, st_sleep, st_poison;
    uint16_t pray_timeout;
    uint8_t  have_pet, pet_hp, pet_kills;
    uint8_t  luckstone_taken;
    uint16_t cnt_kills, cnt_corpses, cnt_reads, cnt_prayers;
    uint8_t  at_str, at_dex, at_con, at_int, at_wis, at_cha;
    uint8_t  pclass, intrinsics, pw, pmaxpw;
    uint8_t  known_spells;
    uint16_t max_dlvl;
    uint8_t  alignment;
    int8_t   luck;
};

/* From here down, all of nexthack.c's CODE is banked into PAGE_22_CODE (mapped
 * into the 0xC000 window on demand). The globals above are DATA and stay
 * resident. The functions main() calls are __banked (see nexthack.h); the
 * static helpers (hunger_*, describe) are reached by in-page calls. */
#ifdef __ZXNEXT
#pragma codeseg PAGE_22_CODE
#pragma constseg PAGE_22_CODE
#else
#pragma codeseg BANK_3
#pragma constseg BANK_3
#endif
/* constseg banks this file's RODATA -- every message/label literal -- next to
 * its code (~1.5 KB off the resident half, the lever that paid for corpses).
 * Safe because every literal is CONSUMED while this bank is mapped: msg/
 * print_str copy to the screen during the call (resident callees don't remap),
 * and the only string pointers that leave the file (hunger_label to
 * draw_status) stay same-bank. Do NOT pass this file's literals as arguments
 * to another bank's __banked function -- the trampoline swaps this bank out. */

/* Write seed + player + each module's state. Returns 1 on success. */
int save_game(void) __banked
{
    uint8_t h = file_create(SAVE_NAME);
    struct save_hdr    hdr;
    struct save_player p;

    if (h == FILE_ERR) return 0;

    hdr.magic = SAVE_MAGIC; hdr.ver = SAVE_VER;
    file_write(h, &hdr, sizeof hdr);

    p.world_seed = world_seed;
    p.hero_x = (int16_t)hero_x; p.hero_y = (int16_t)hero_y;
    p.dlvl = dlvl;   p.turns = turns;
    p.php = php;     p.pmaxhp = pmaxhp;
    p.gold = gold;   p.nutrition = nutrition;
    p.xp = xp;       p.xlvl = xlvl; p.has_amulet = has_amulet;
    p.st_conf = st_conf;   p.st_blind = st_blind;
    p.st_sleep = st_sleep; p.st_poison = st_poison;
    p.pray_timeout = pray_timeout;
    p.have_pet = have_pet; p.pet_hp = pet_hp; p.pet_kills = pet_kills;
    p.luckstone_taken = luckstone_taken;
    p.cnt_kills = cnt_kills; p.cnt_corpses = cnt_corpses;
    p.cnt_reads = cnt_reads; p.cnt_prayers = cnt_prayers;
    p.at_str = at_str; p.at_dex = at_dex; p.at_con = at_con;
    p.at_int = at_int; p.at_wis = at_wis; p.at_cha = at_cha;
    p.pclass = pclass; p.intrinsics = intrinsics;
    p.pw = pw; p.pmaxpw = pmaxpw;
    p.known_spells = known_spells;
    p.max_dlvl = max_dlvl;
    p.alignment = alignment;
    p.luck = luck;
    file_write(h, &p, sizeof p);

    item_save(h);
    level_save(h);
    monster_save(h);
    file_close(h);
    return 1;
}

/* Load a saved game and delete the file (so it cannot be reloaded - the
 * NetHack anti-save-scum rule). Returns 1 if a valid save was restored. */
int load_game(void) __banked
{
    uint8_t h = file_open(SAVE_NAME);
    struct save_hdr    hdr;
    struct save_player p;

    if (h == FILE_ERR) return 0;

    file_read(h, &hdr, sizeof hdr);
    if (hdr.magic != SAVE_MAGIC || hdr.ver != SAVE_VER) {
        file_close(h);
        file_remove(SAVE_NAME);     /* discard an incompatible save */
        return 0;
    }

    file_read(h, &p, sizeof p);
    world_seed = p.world_seed;
    hero_x = p.hero_x; hero_y = p.hero_y;
    dlvl = p.dlvl;     turns = p.turns;
    php = p.php;       pmaxhp = p.pmaxhp;
    gold = p.gold;     nutrition = p.nutrition;
    xp = p.xp;         xlvl = p.xlvl; has_amulet = p.has_amulet;
    st_conf = p.st_conf;   st_blind = p.st_blind;
    st_sleep = p.st_sleep; st_poison = p.st_poison;
    pray_timeout = p.pray_timeout;
    have_pet = p.have_pet; pet_hp = p.pet_hp; pet_kills = p.pet_kills;
    luckstone_taken = p.luckstone_taken;
    cnt_kills = p.cnt_kills; cnt_corpses = p.cnt_corpses;
    cnt_reads = p.cnt_reads; cnt_prayers = p.cnt_prayers;
    at_str = p.at_str; at_dex = p.at_dex; at_con = p.at_con;
    at_int = p.at_int; at_wis = p.at_wis; at_cha = p.at_cha;
    pclass = p.pclass; intrinsics = p.intrinsics;
    pw = p.pw; pmaxpw = p.pmaxpw;
    known_spells = p.known_spells;
    max_dlvl = p.max_dlvl;
    alignment = p.alignment;
    luck = p.luck;
    dead = 0; won = 0;

    item_load(h);
    level_load(h);
    monster_load(h);
    file_close(h);
    file_remove(SAVE_NAME);
    return 1;
}

/* ============================================================
 * Level orchestration
 * ============================================================ */

/* Build the current dlvl: terrain + gold, then monsters (which must see the
 * freshly placed gold), then re-apply remembered mutations. The order keeps
 * generation deterministic across revisits. */
/* room rects (defined in levelgen.c) -- read here to drop an altar in one */
extern uint8_t r_x[], r_y[], r_w[], r_h[];

/* Some levels hold an altar. A side hash of (world_seed, dlvl) -- never rn2, so
 * the deterministic per-depth persistence stays in sync -- picks roughly one
 * level in five and one of its rooms; we drop a '_' on that room's centre, but
 * only when it is plain floor (so it never buries stairs, a door or an item).
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
    if (lvl[cy][cx] == '.') lvl[cy][cx] = '_';
}

/* Some levels have a fountain, chosen the same rn2-free way as the altar (a
 * different hash constant, so the two rarely land together and never overwrite
 * -- both guard on '.'). Roughly one level in four from depth 2 down. */
static void place_fountain(void)
{
    uint16_t h;
    uint8_t room, cx, cy;

    if (rcount == 0 || dlvl < 2) return;
    h = (uint16_t)(world_seed * 3u + (uint16_t)dlvl * 0x2C9Fu);
    if ((h % 4u) != 0) return;
    room = (uint8_t)((h >> 4) % rcount);
    cx = (uint8_t)(r_x[room] + r_w[room] / 2);
    cy = (uint8_t)(r_y[room] + r_h[room] / 2);
    if (lvl[cy][cx] == '.') lvl[cy][cx] = '{';
}

static void traps_reset(void);   /* defined with the trap code, below */

/* (Re)place the pet next to the hero. Called after the hero's position is set
 * on every level entry (new game, descend/ascend, trap-door fall, restore), so
 * the dog always tags along. It takes the tail monster slot (after the random
 * mobs and any keeper), which the uint8_t mon_dead bitmask never tracks, and is
 * never persisted -- pet_idx/pet_hp carry it instead. If the hero arrived in a
 * spot with no free adjacent floor (a tight corridor), the pet sits out this
 * level and rejoins on the next. (Lives here, not monster_ai.c, to keep that
 * bank under 16 KB.) */
void place_pet(void) __banked
{
    int dx, dy;
    pet_idx = -1;
    if (!have_pet || mcount >= MAXMON) return;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int x = hero_x + dx, y = hero_y + dy;
            char c;
            if ((dx == 0 && dy == 0) || pet_idx >= 0) continue;
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

/* ============================================================
 * Rendering
 * ============================================================ */

/* telepathy: while blind you sense every monster on the level (the floating
 * eye's gift) -- both renderers draw monsters through this despite no vision */
#define mon_sensed() (st_blind && (intrinsics & INTR_TELEPATHY))

#ifdef __ZXNEXT
void draw_map(void) __banked
{
    const uint8_t *seen = fov_bitmap();   /* explored bitmap        */
    const uint8_t *vis  = vis_bitmap();   /* visible-this-turn map  */
    uint16_t idx = 0;
    uint8_t x, y, t, attr;
    int mi;
    /* the shop room's bounds (read once), so its walls render in warm bricks */
    uint8_t sx, sy, sw, sh, sx1 = 0, sy1 = 0;
    int has_shop = shop_rect(&sx, &sy, &sw, &sh);
    if (has_shop) { sx1 = (uint8_t)(sx + sw - 1); sy1 = (uint8_t)(sy + sh - 1); }

    /* Single pass, running tilemap pointer, inline FOV bit tests (kept out of
     * per-cell function calls so held-key movement stays fluid). Fog of war:
     * unexplored = dark; in sight = full colour; remembered (seen but out of
     * sight) = dimmed (palette offset 1); monsters only show while in sight. */
    for (y = 0; y < MAPH; y++) {
        uint8_t *p = tm_cell_ptr(OX, (uint8_t)(OY + y));
        for (x = 0; x < MAPW; x++, idx++) {
            uint8_t byte = (uint8_t)(idx >> 3);
            uint8_t mask = (uint8_t)(1u << (idx & 7));
            attr = 0;
            if (x == (uint8_t)hero_x && y == (uint8_t)hero_y) {
                t = T_HERO;
            } else if (mon_sensed() && (mi = monster_at(x, y)) >= 0) {
                t = mon_tile(m_type[mi]);         /* sensed by telepathy  */
            } else if (!(seen[byte] & mask)) {
                t = T_ROCK;                       /* never seen -> dark   */
            } else if (vis[byte] & mask) {        /* in sight -> full     */
                mi = monster_at(x, y);
                t = (mi >= 0) ? mon_tile(m_type[mi]) : tile_for(lvl[y][x]);
            } else {                              /* remembered -> dim    */
                t = tile_for(lvl[y][x]);
                attr = 0x10;                      /* palette offset 1     */
            }
            if (has_shop && (t == T_WALL || t == T_MINEWALL) &&
                x >= sx && x <= sx1 && y >= sy && y <= sy1)
                t = T_SHOPWALL;   /* shop walls: warm bricks -- Minetown's
                                   * shop too (its walls resolve T_MINEWALL) */
            *p++ = t;
            *p++ = attr;
        }
    }
}
#else
#define VIEW_EDGE   6
#define VIEW_SHADOW ((uint8_t *)0x6000u)
static uint8_t vx_origin;

/* draw_map render state, shared with its per-cell helpers (set each call). */
static const uint8_t *dm_seen, *dm_vis;
static uint8_t dm_shop, dm_sx, dm_sy, dm_sx1, dm_sy1;
/* what draw_map put on screen last turn, so the fast path can erase the hero and
 * monsters it drew and repaint only the cells that moved. 255 = "nothing here". */
static uint8_t  prev_hx = 255, prev_hy = 255;
static uint8_t  prev_mx[MAXMON], prev_my[MAXMON];
static uint16_t prev_vis_sum = 0xFFFF;
static uint8_t  mon_bm[(MAPH * TM_W + 7) / 8];   /* viewport cells a monster covers */
/* copy of the vis bitmap the screen was LAST PAINTED with, for the mid path's
 * XOR (210 B). In Bank 5's free gap after fov_pool (0x6880+840=0x6BC8), clear
 * of the BFS scratch at 0x7400. Synced only when a path repaints by vis (mid/
 * full), so it always mirrors the screen -- a vis-hash collision that leaves
 * stale cells self-repairs on the next mid pass. */
#define PREV_VIS ((uint8_t *)0x6C00u)

/* write one viewport cell ONLY if it differs from the shadow (write-once diff). */
static void dm_paint(uint8_t mapx, uint8_t mapy, uint8_t vx, uint8_t t, uint8_t attr)
{
    uint8_t *shad = VIEW_SHADOW, sc;
    uint16_t si;
    if (mapx < vx || mapx >= (uint8_t)(vx + TM_W) || mapy >= MAPH) return;
    sc = (uint8_t)(mapx - vx);
    si = (uint16_t)(((uint16_t)mapy * TM_W + sc) << 1);
    if (shad[si] != t || shad[si + 1] != attr) {
        puttile_attr(sc, (uint8_t)(OY + mapy), t, attr);
        shad[si] = t; shad[si + 1] = attr;
    }
}

/* paint a cell's TERRAIN (the pass-1 non-hero logic) -- used by the fast path to
 * erase the cell where the hero or a monster was. Uses the dm_* state above. */
static void dm_terrain(uint8_t mapx, uint8_t mapy, uint8_t vx)
{
    uint16_t idx  = (uint16_t)mapy * MAPW + mapx;
    uint8_t  byte = (uint8_t)(idx >> 3), mask = (uint8_t)(1u << (idx & 7)), t, attr;
    if (!(dm_seen[byte] & mask)) { t = T_ROCK; attr = 0; }
    else {
        t = tile_for(lvl[mapy][mapx]);
        if (dm_shop && (t == T_WALL || t == T_MINEWALL) &&   /* Minetown too */
            mapx >= dm_sx && mapx <= dm_sx1 && mapy >= dm_sy && mapy <= dm_sy1) t = T_SHOPWALL;
        attr = (dm_vis[byte] & mask) ? (uint8_t)(udg_ink[t - T_ROCK] | 0x40)
                                     : udg_ink[t - T_ROCK];
    }
    dm_paint(mapx, mapy, vx, t, attr);
}

void draw_map(void) __banked
{
    uint8_t *shad = VIEW_SHADOW;
    uint8_t sc, x, y, t, attr, vx, full, i;
    uint8_t pv_sync = 0;    /* repainted by vis (mid/full): resync PREV_VIS */
    int hsc, nvx;
    uint8_t sx, sy, sw, sh, sx1 = 0, sy1 = 0;
    int has_shop = shop_rect(&sx, &sy, &sw, &sh);
    if (has_shop) { sx1 = (uint8_t)(sx + sw - 1); sy1 = (uint8_t)(sy + sh - 1); }
    dm_seen = fov_bitmap(); dm_vis = vis_bitmap();
    dm_shop = (uint8_t)has_shop; dm_sx = sx; dm_sy = sy; dm_sx1 = sx1; dm_sy1 = sy1;

    /* keep the viewport origin unless the hero left the central band (or a full
     * redraw is pending), then recenter -- a change forces a full redraw. */
    full = map_dirty;
    if (map_dirty) { nvx = hero_x - TM_W / 2; map_dirty = 0; }
    else {
        hsc = hero_x - (int)vx_origin;
        nvx = (hsc >= VIEW_EDGE && hsc <= TM_W - 1 - VIEW_EDGE)
              ? (int)vx_origin : hero_x - TM_W / 2;
    }
    if (nvx < 0) nvx = 0;
    if (nvx > MAPW - TM_W) nvx = MAPW - TM_W;
    if ((uint8_t)nvx != vx_origin) { vx_origin = (uint8_t)nvx; full = 1; }
    vx = vx_origin;

    if (!full && !map_flush) {
        /* FAST/MID PATH: the viewport didn't move and no cell changed content.
         * If the visible set is unchanged (moving within a lit room) only the
         * entities below repaint. If it CHANGED (a corridor step reveals/hides
         * cells) first repaint exactly the cells whose visibility bit flipped:
         * XOR the vis bitmap against a copy of the one the screen was last
         * painted with (PREV_VIS, Bank 5). A corridor step flips ~10-30 cells,
         * so this replaces the old all-672-cell sweep that made corridor walking
         * visibly heavier than room walking. MAPW is 80, so each map row is
         * exactly 10 bitmap bytes -- no division anywhere. */
        if (fov_vis_sum != prev_vis_sum) {
            uint8_t *pv = PREV_VIS;
            uint8_t yy, bb, d, bit;
            uint16_t bidx = 0;
            pv_sync = 1;
            for (yy = 0; yy < MAPH; yy++)
                for (bb = 0; bb < MAPW / 8; bb++, bidx++) {
                    d = (uint8_t)(dm_vis[bidx] ^ pv[bidx]);
                    if (!d) continue;
                    for (bit = 0; bit < 8; bit++)
                        if (d & (uint8_t)(1u << bit))
                            dm_terrain((uint8_t)((bb << 3) + bit), yy, vx);
                }
        }
        /* Erase the hero's old cell ONLY if it moved; the redraw below is a
         * no-op via the shadow diff when nothing changed. Erasing+redrawing an
         * unmoved cell every turn is what made a stationary dog flicker. */
        if (prev_hx != 255 &&
            (prev_hx != (uint8_t)hero_x || prev_hy != (uint8_t)hero_y))
            dm_terrain(prev_hx, prev_hy, vx);
        dm_paint((uint8_t)hero_x, (uint8_t)hero_y, vx, T_HERO,
                 (uint8_t)(udg_ink[T_HERO - T_ROCK] | 0x40));
        for (i = 0; i < MAXMON; i++) {
            uint8_t cmx = 255, cmy = 255;   /* this turn's drawn cell, or 255 = none */
            if (i < mcount && m_alive[i] && m_y[i] < MAPH &&
                m_x[i] >= vx && m_x[i] < (uint8_t)(vx + TM_W) &&
                !(m_x[i] == (uint8_t)hero_x && m_y[i] == (uint8_t)hero_y)) {
                uint16_t midx = (uint16_t)m_y[i] * MAPW + m_x[i];
                if ((dm_vis[midx >> 3] & (1u << (midx & 7))) || mon_sensed())
                    { cmx = m_x[i]; cmy = m_y[i]; }
            }
            /* erase only where it left -- but never a cell someone occupies NOW:
             * the hero (a pet swap puts its old cell under the just-drawn hero)
             * or another monster (the pet<->keeper swap moves a LOWER slot onto
             * the pet's old cell, so ascending draw order no longer guarantees
             * the enterer is drawn after this erase). The occupant is visible
             * whenever this path runs, so it is (re)drawn this same frame. */
            if (prev_mx[i] != 255 && (prev_mx[i] != cmx || prev_my[i] != cmy) &&
                !(prev_mx[i] == (uint8_t)hero_x && prev_my[i] == (uint8_t)hero_y) &&
                monster_at(prev_mx[i], prev_my[i]) < 0)
                dm_terrain(prev_mx[i], prev_my[i], vx);
            if (cmx != 255) {
                t = mon_tile(m_type[i]);
                dm_paint(cmx, cmy, vx, t, (uint8_t)(udg_ink[t - T_ROCK] | 0x40));
            }
        }
    } else {
        /* FULL redraw (level entry, scroll, or the FOV changed). Draw the MONSTERS
         * FIRST (and mark their cells), then terrain skipping those cells. Drawing
         * a monster's new cell before the terrain pass erases its old cell means a
         * moving monster is never briefly absent -- that gap was the corridor
         * flicker. (Terrain under it stays correct: when it moves on, its old cell
         * is no longer marked, so the terrain pass repaints floor there.) */
        pv_sync = 1;
        if (full) {
            /* A forced full redraw means the screen may hold ANYTHING -- the
             * inventory/help overlay that just closed wrote text straight to
             * the ULA without touching this shadow. dm_paint trusts the shadow
             * to skip "unchanged" cells, so a stale entry left an overlay
             * letter sitting on an unmoved monster (the corrupted-dog bug: the
             * terrain sweep force-writes on full, but the monster/erase passes
             * go through dm_paint's diff). Invalidate the whole shadow so
             * every diff misses exactly once. */
            uint16_t bz;
            for (bz = 0; bz < (uint16_t)(MAPH * TM_W) * 2; bz++) shad[bz] = 0xFF;
        }
        { uint16_t bz; for (bz = 0; bz < sizeof mon_bm; bz++) mon_bm[bz] = 0; }
        for (i = 0; i < mcount; i++) {
            uint16_t midx, vb;
            uint8_t mt;
            if (!m_alive[i] || m_y[i] >= MAPH) continue;
            if (m_x[i] < vx || m_x[i] >= (uint8_t)(vx + TM_W)) continue;
            if (m_x[i] == (uint8_t)hero_x && m_y[i] == (uint8_t)hero_y) continue;
            midx = (uint16_t)m_y[i] * MAPW + m_x[i];
            if (!(dm_vis[midx >> 3] & (1u << (midx & 7))) && !mon_sensed()) continue;
            vb = (uint16_t)m_y[i] * TM_W + (uint16_t)(m_x[i] - vx);
            mon_bm[vb >> 3] |= (uint8_t)(1u << (vb & 7));
            mt = mon_tile(m_type[i]);
            dm_paint(m_x[i], m_y[i], vx, mt, (uint8_t)(udg_ink[mt - T_ROCK] | 0x40));
        }
        /* erase where each monster was drawn last turn (unless a monster is there
         * now) RIGHT AWAY -- not in the slow terrain pass below -- so the old cell
         * doesn't linger as a ghost while the terrain pass grinds toward it. */
        for (i = 0; i < mcount; i++) {
            uint8_t px = prev_mx[i], py = prev_my[i];
            uint16_t pvb;
            if (px == 255 || px < vx || px >= (uint8_t)(vx + TM_W) || py >= MAPH) continue;
            if (px == (uint8_t)hero_x && py == (uint8_t)hero_y) continue;  /* hero is here now */
            pvb = (uint16_t)py * TM_W + (uint16_t)(px - vx);
            if (mon_bm[pvb >> 3] & (1u << (pvb & 7))) continue;   /* a monster is here now */
            dm_terrain(px, py, vx);
        }
        for (y = 0; y < MAPH; y++) {
            uint16_t idx = (uint16_t)y * MAPW + vx;
            uint16_t si  = (uint16_t)((uint16_t)y * TM_W) << 1;
            uint16_t vb  = (uint16_t)y * TM_W;
            const char *lrow = lvl[y];
            for (sc = 0; sc < TM_W; sc++, idx++, si += 2, vb++) {
                uint8_t byte, mask;
                if (mon_bm[vb >> 3] & (1u << (vb & 7))) continue;   /* a monster is here */
                byte = (uint8_t)(idx >> 3);
                mask = (uint8_t)(1u << (idx & 7));
                x = (uint8_t)(vx + sc);
                if (x == (uint8_t)hero_x && y == (uint8_t)hero_y) {
                    t = T_HERO;
                    attr = (uint8_t)(udg_ink[T_HERO - T_ROCK] | 0x40);
                } else if (!(dm_seen[byte] & mask)) {
                    t = T_ROCK; attr = 0;
                } else {
                    t = tile_for(lrow[x]);
                    if (has_shop && (t == T_WALL || t == T_MINEWALL) &&
                        x >= sx && x <= sx1 && y >= sy && y <= sy1) t = T_SHOPWALL;
                    attr = (dm_vis[byte] & mask)
                           ? (uint8_t)(udg_ink[t - T_ROCK] | 0x40)
                           : udg_ink[t - T_ROCK];
                }
                if (full || shad[si] != t || shad[si + 1] != attr) {
                    puttile_attr(sc, (uint8_t)(OY + y), t, attr);
                    shad[si] = t; shad[si + 1] = attr;
                }
            }
        }
    }

    /* remember what is on screen now, for next turn's fast path: each monster's
     * DRAWN cell (255 = not drawn), so next turn erases exactly where it left. */
    prev_hx = (uint8_t)hero_x; prev_hy = (uint8_t)hero_y;
    for (i = 0; i < MAXMON; i++) {
        uint8_t dr = 0;
        if (i < mcount && m_alive[i] && m_y[i] < MAPH &&
            m_x[i] >= vx && m_x[i] < (uint8_t)(vx + TM_W) &&
            !(m_x[i] == (uint8_t)hero_x && m_y[i] == (uint8_t)hero_y)) {
            uint16_t midx = (uint16_t)m_y[i] * MAPW + m_x[i];
            if ((dm_vis[midx >> 3] & (1u << (midx & 7))) || mon_sensed()) dr = 1;
        }
        if (dr) { prev_mx[i] = m_x[i]; prev_my[i] = m_y[i]; }
        else      prev_mx[i] = 255;
    }
    prev_vis_sum = fov_vis_sum;
    map_flush = 0;                    /* the distant change (if any) is on screen */
    if (pv_sync) {                    /* PREV_VIS mirrors what is on screen now */
        uint8_t *pv = PREV_VIS;
        uint16_t b2;
        for (b2 = 0; b2 < (uint16_t)(MAPH * (MAPW / 8)); b2++) pv[b2] = dm_vis[b2];
    }
}
#endif

static uint8_t hunger_now(void)
{
    if (nutrition <= 0)  return 3;
    if (nutrition < 50)  return 2;
    if (nutrition < 150) return 1;
    return 0;
}

static const char *hunger_label(void)
{
    if (nutrition >= 1000) return "Satiated";
    switch (hunger_state) {
    case 1:  return "Hungry";
    case 2:  return "Weak";
    case 3:  return "Fainting";
    default: return "";
    }
}

static uint8_t hunger_color(void)
{
    if (nutrition >= 1000)  return C_GREEN | C_BRIGHT;
    if (hunger_state >= 2)  return C_RED | C_BRIGHT;
    if (hunger_state == 1)  return C_YELLOW | C_BRIGHT;
    return C_GREEN | C_BRIGHT;
}

/* once-per-turn upkeep: hunger ticks down, HP slowly regenerates (or you
 * starve when out of food) */
void upkeep(void) __banked
{
    uint8_t hs;

    if (nutrition > -50)
        nutrition--;

    hs = hunger_now();
    if (hs > hunger_state) {
        if (hs == 1)      msg("You begin to feel hungry.");
        else if (hs == 2) msg("You are weak from hunger.");
        else if (hs == 3) msg("You faint from lack of food!");
    }
    hunger_state = hs;

    if (nutrition <= 0) {                       /* starving */
        if ((turns & 3) == 0 && php > 0) {
            php--;
            if (php == 0) dead = 1;
        }
    } else {
        uint8_t hr = (uint8_t)(at_con >= 16 ? 14 :
                               at_con >= 13 ? 17 : 20);
        if (regen_ring) hr >>= 1;               /* the ring mends twice as fast */
        if (++heal_timer >= hr) {
            heal_timer = 0;                     /* slow regeneration -- a hardy
                                                 * constitution mends faster */
            if (php < pmaxhp) php++;
        }
    }
    if (pw < pmaxpw && ++pw_timer >= (uint8_t)(at_wis >= 14 ? 12 : 18)) {
        pw_timer = 0;                           /* power trickles back; wisdom
                                                 * quickens the flow */
        pw++;
    }

    /* the dog mends between fights too, and can toughen a little (8 -> 12) if
     * kept alive, so a careful companion is worth keeping rather than doomed */
    if (have_pet && (turns & 15) == 0 &&      /* regen cap grows with the dog */
        pet_hp < (uint8_t)(12 + (pet_kills >= 12 ? 12 : pet_kills >= 4 ? 6 : 0)))
        pet_hp++;

    /* transient status effects tick down; poison gnaws a hit point each turn */
    if (st_poison) {
        if (php > 0) php--;
        if (php == 0) dead = 1;
        if (!--st_poison && !dead) msg("The poison wears off.");
    }
    if (st_conf  && !--st_conf)  msg("Your head clears.");
    if (st_blind && !--st_blind) { msg("You can see again."); map_dirty = 1; }
    if (st_sleep && !--st_sleep) msg("You wake up.");
    if (el_life && !--el_life)   msg("The engraving fades away.");
    if (pray_timeout) pray_timeout--;

    maybe_spawn_wanderer();                     /* the dungeon refills over time */
}

/* Pray to your god. It mends your worst affliction, then ignores you for a
 * while (pray_timeout); praying again too soon is simply refused. Standing on
 * an altar, the gods lift curses from your whole pack, not just what you wear. */
void do_pray(void) __banked
{
    if (pray_timeout) {
        msg("You have prayed too recently.");
        return;                         /* a refused prayer costs no turn */
    }
    cnt_prayers++;                      /* the attempt breaks the conduct */
    if (has_amulet) {                   /* Moloch owns the air down here */
        msg("Moloch drowns out your prayer!");
        turns++; acted = 1;
        return;
    }
    if (eff_luck() < 0) {               /* the gods remember your affronts */
        msg("You feel forsaken.");
        pray_timeout = 100;             /* and they take their time forgiving */
        turns++; acted = 1;
        return;
    }
    if (nutrition < 50) {
        nutrition = 900;                msg("Your hunger is satisfied.");
    } else if (php < 5 || (uint16_t)php * 7 < pmaxhp) {
        php = pmaxhp;                    msg("You feel much better.");
    } else if (st_poison || st_blind || st_conf) {
        st_poison = st_blind = st_conf = 0;
                                        msg("You feel purified.");
    } else if (pray_uncurse(terrain(hero_x, hero_y) == '_')) {
                                        msg("A weight lifts from your pack.");
    } else {
        if (php < pmaxhp) php++;         msg("You feel full of awe.");
    }
    pray_timeout = (uint16_t)(300 + rn2(150));
    turns++; acted = 1;
}

#ifdef __ZXNEXT
/* The Next shows the key list on the shared '?' screen (show_help), not a
 * persistent command bar -- the bar could not grow without overflowing PAGE_22
 * (which pushed the Layer 2 palette out of bank 11 and wrecked the title
 * colours). draw_help leaves one discreet pointer to '?' on row 24 (below the
 * status bar, untouched by the per-turn map/status redraws). */
void draw_help(void) __banked
{
    print_str(0, 24, "Press  ?  for the keys and command list.", C_CYAN | C_BRIGHT);
}
#else
void draw_help(void) __banked
{
    /* The 24-row ULA has no room for a persistent help bar (row 0 = message,
     * rows 1-21 = map, rows 22-23 = status), so the keys live on a '?' screen
     * (show_help) instead. */
}
#endif

/* Alignment name (0 Lawful / 1 Neutral / 2 Chaotic). Static: every caller
 * (status, help, describe) is in this file's bank and copies it to the screen
 * while that bank is mapped, so the constseg'd literal never crosses a bank. */
static const char *align_name(uint8_t a)
{
    return a == 0 ? "Lawful" : a == 1 ? "Neutral" : "Chaotic";
}

/* '?': a full-screen key list, then restore the playfield. ONE implementation
 * for both targets (the Next had a 2-line command bar before, but its strings
 * are resident rodata and grew PAGE_22 -- a single '?' screen, 32 columns wide
 * so it fits the 128K ULA too, is cheaper and keeps the two ports consistent).
 * On the Next it sits in the top-left; the row-24 pointer leads players to it. */
void show_help(void) __banked
{
    tm_cls();
    print_str(8,  1, "NextHack  Keys",       C_WHITE | C_BRIGHT);
    print_str(2,  3, "Move: arrow keys, or", C_CYAN | C_BRIGHT);
    print_str(7,  4, "h j k l  y u b n",     C_CYAN | C_BRIGHT);
    print_str(2,  5, "Stairs: > < or Enter", C_CYAN | C_BRIGHT);
    print_str(2,  6, "s search   . wait",    C_CYAN | C_BRIGHT);
    print_str(2,  8, ", pick up",            C_CYAN | C_BRIGHT);
    print_str(2,  9, "i inventory",          C_CYAN | C_BRIGHT);
    print_str(2, 10, "w wield    W wear",    C_CYAN | C_BRIGHT);
    print_str(2, 11, "P ring     t throw",   C_CYAN | C_BRIGHT);
    print_str(2, 12, "q quaff    e eat",     C_CYAN | C_BRIGHT);
    print_str(2, 13, "r read     p pray",    C_CYAN | C_BRIGHT);
    print_str(2, 14, "E engrave Elbereth",   C_CYAN | C_BRIGHT);
    print_str(2, 15, "d drop     S save",    C_CYAN | C_BRIGHT);
    print_str(2, 16, "z zap  Z cast  ? help", C_CYAN | C_BRIGHT);
    {   /* the character sheet -- the 32-col 128K status bar has no room for
         * it, so both targets show it here */
        uint8_t x = print_str(2, 17, class_name(), C_YELLOW | C_BRIGHT);
        (void)x;
        x = print_str(2, 18, "St:", C_GREEN | C_BRIGHT);
        x = put_uint(x, 18, at_str, C_GREEN | C_BRIGHT);
        x = print_str(x, 18, " Dx:", C_GREEN | C_BRIGHT);
        x = put_uint(x, 18, at_dex, C_GREEN | C_BRIGHT);
        x = print_str(x, 18, " Co:", C_GREEN | C_BRIGHT);
        x = put_uint(x, 18, at_con, C_GREEN | C_BRIGHT);
        x = print_str(x, 18, " In:", C_GREEN | C_BRIGHT);
        x = put_uint(x, 18, at_int, C_GREEN | C_BRIGHT);
        x = print_str(x, 18, " Wi:", C_GREEN | C_BRIGHT);
        x = put_uint(x, 18, at_wis, C_GREEN | C_BRIGHT);
        x = print_str(x, 18, " Ch:", C_GREEN | C_BRIGHT);
        put_uint(x, 18, at_cha, C_GREEN | C_BRIGHT);
        print_str(2, 19, align_name(alignment), C_CYAN | C_BRIGHT);
    }
    print_str(4, 20, "Press any key...",     C_WHITE | C_BRIGHT);
    in_wait_nokey();
    getkey();
    in_wait_nokey();
    map_dirty = 1;
    draw_help();            /* Next: redraw the row-24 pointer (no-op on 128K) */
    draw_status();
    draw_map();
}

#ifdef __ZXNEXT
void draw_status(void) __banked
{
    uint8_t x;
    const char *h = hunger_label();

    /* Each status cell is written exactly once (no clear-then-fill) so the
     * status bar does not flicker as values change. */
    x = print_str(0, 22, "Player the ", C_GREEN | C_BRIGHT);
    x = print_str(x, 22, class_name(), C_GREEN | C_BRIGHT);  /* same bank */
    while (x < 22) putcell(x++, 22, ' ', C_GREEN);
    x = print_str(x, 22, "St:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 22, at_str, C_GREEN | C_BRIGHT);
    x = print_str(x, 22, " Dx:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 22, at_dex, C_GREEN | C_BRIGHT);
    x = print_str(x, 22, " Co:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 22, at_con, C_GREEN | C_BRIGHT);
    x = print_str(x, 22, " In:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 22, at_int, C_GREEN | C_BRIGHT);
    x = print_str(x, 22, " Wi:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 22, at_wis, C_GREEN | C_BRIGHT);
    x = print_str(x, 22, " Ch:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 22, at_cha, C_GREEN | C_BRIGHT);
    x = print_str(x, 22, "  ", C_GREEN | C_BRIGHT);
    x = print_str(x, 22, align_name(alignment), C_GREEN | C_BRIGHT);
    while (x < 68) putcell(x++, 22, ' ', C_GREEN);
    x = print_str(68, 22, h, hunger_color());      /* hunger state at the tail */
    while (x < 80) putcell(x++, 22, ' ', C_GREEN);

    x = print_str(0, 23, IN_MINES(dlvl) ? "Mine:" : "Dlvl:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, IN_MINES(dlvl) ? (uint16_t)(dlvl - MINES_BASE + 1) : dlvl,
                 C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  ", C_GREEN | C_BRIGHT);
    puttile(x, 23, T_DOLLAR); x++;        /* green '$' tile (ROM '$' is blank) */
    x = print_str(x, 23, ":", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, gold, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  HP:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, php, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "(", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, pmaxhp, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, ")  Pw:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, pw, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "(", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, pmaxpw, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, ")  AC:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, ac, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  Xp:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, xlvl, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "/", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, xp, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  T:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, turns, C_GREEN | C_BRIGHT);
    if (st_conf)   x = print_str(x, 23, "  Conf",   C_RED | C_BRIGHT);
    if (st_blind)  x = print_str(x, 23, "  Blind",  C_RED | C_BRIGHT);
    if (st_sleep)  x = print_str(x, 23, "  Asleep", C_RED | C_BRIGHT);
    if (st_poison) x = print_str(x, 23, "  Poison", C_RED | C_BRIGHT);
    while (x < 80) putcell(x++, 23, ' ', C_GREEN);
}
#else
/* The status bar (rows 22-23) is rebuilt every turn but most cells -- the
 * labels, the spaces -- never change, so a write-through diff against a shadow
 * skips the blit when a cell is unchanged. The shadow holds (glyph, colour)
 * per cell in Bank 5 RAM, right after the map's VIEW_SHADOW (0x6000 + 21*32*2
 * = 0x6540), well below the BFS scratch at 0x7400. A normal turn then redraws
 * only the few digits that moved (T:, HP, gold) instead of all ~64 cells. */
#define SSHADOW ((uint8_t *)0x6600u)
static uint8_t sd_force;                /* 1 = redraw every cell (see draw_status) */

/* glyph >= 128 is a UDG tile (the '$'); below that an ROM-font char. */
static void sd_putc(uint8_t x, uint8_t y, uint8_t glyph, uint8_t coff)
{
    uint8_t *s = SSHADOW + (((uint16_t)(y - 22) * TM_W + x) << 1);
    if (sd_force || s[0] != glyph || s[1] != coff) {
        if (glyph >= 128) puttile(x, y, glyph);
        else              putcell(x, y, glyph, coff);
        s[0] = glyph; s[1] = coff;
    }
}
static uint8_t sd_str(uint8_t x, uint8_t y, const char *p, uint8_t coff)
{
    while (*p && x < TM_W) { sd_putc(x++, y, (uint8_t)*p, coff); p++; }
    return x;
}
static uint8_t sd_uint(uint8_t x, uint8_t y, uint16_t v, uint8_t coff)
{
    char t[5];
    uint8_t n = 0;
    if (v == 0) { sd_putc(x++, y, '0', coff); return x; }
    while (v) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) sd_putc(x++, y, (uint8_t)t[--n], coff);
    return x;
}

void draw_status(void) __banked
{
    uint8_t x;
    const char *h = hunger_label();

    /* Reuse the renderer's full-redraw flag instead of a second one: every
     * draw_status() call site is immediately followed by draw_map() (which
     * then clears map_dirty), so reading it here -- without consuming it --
     * forces a full status redraw after any overlay/level change/load, and a
     * cheap diff on ordinary turns. The shadow is garbage at boot, but boot
     * has map_dirty == 1, so the first pass writes (and seeds) every cell. */
    sd_force = map_dirty;

    x = sd_str(0, 22, IN_MINES(dlvl) ? "Mine:" : "Dlvl:", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 22, IN_MINES(dlvl) ? (uint16_t)(dlvl - MINES_BASE + 1) : dlvl,
                C_GREEN | C_BRIGHT);
    x = sd_str(x, 22, " ", C_GREEN | C_BRIGHT);
    sd_putc(x, 22, T_DOLLAR, 0); x++;     /* green '$' tile (ROM '$' is blank) */
    x = sd_str(x, 22, ":", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 22, gold, C_GREEN | C_BRIGHT);
    x = sd_str(x, 22, " HP:", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 22, php, C_GREEN | C_BRIGHT);
    x = sd_str(x, 22, "/", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 22, pmaxhp, C_GREEN | C_BRIGHT);
    while (x < TM_W - 6) sd_putc(x++, 22, ' ', C_GREEN);     /* pad up to the hint */
    sd_str(TM_W - 6, 22, "?=help", C_GREEN | C_BRIGHT);      /* so the player finds show_help */

    x = sd_str(0, 23, "AC:", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 23, ac, C_GREEN | C_BRIGHT);
    x = sd_str(x, 23, " Xp:", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 23, xlvl, C_GREEN | C_BRIGHT);
    x = sd_str(x, 23, " T:", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 23, turns, C_GREEN | C_BRIGHT);
    if (st_conf)   x = sd_str(x, 23, " Conf", C_RED | C_BRIGHT);
    if (st_blind)  x = sd_str(x, 23, " Blnd", C_RED | C_BRIGHT);
    if (st_sleep)  x = sd_str(x, 23, " Slp",  C_RED | C_BRIGHT);
    if (st_poison) x = sd_str(x, 23, " Pois", C_RED | C_BRIGHT);
    if (*h) {
        x = sd_str(x, 23, " ", C_GREEN | C_BRIGHT);
        x = sd_str(x, 23, h, hunger_color());
    }
    while (x < TM_W) sd_putc(x++, 23, ' ', C_GREEN);
}
#endif

/* ============================================================
 * Player actions
 * ============================================================ */

static void describe(char dest, int moved)
{
    if (!moved) {
        if (dest == ' ')      msg("It's solid stone.");
        else                  msg("It's a wall.");
        return;
    }
    switch (dest) {
    case '>': msg("There is a staircase down here."); break;
    case '<': msg("There is a staircase up here.");   break;
    case '"': msg("The Amulet of Yendor! (,get)"); break;
    case ')': case '[': case '!': case '%': case '?': case '=': case '/':
    case '&':
        msg2(floor_item_desc(),
             shop_in_room(hero_x, hero_y) ? " (,buy)" : " (,get)", "");
        break;
    case '_': msg2("A ",
                   align_name(altar_align((uint8_t)hero_x, (uint8_t)hero_y)),
                   " altar. (d to offer)");           break;
    case 'v': msg("A mine entrance. (> descends)");   break;
    case '*': msg("A luckstone! (, to take)");        break;
    case '{': msg("There is a fountain here.");       break;
    default:  msg("");                                break;
    }
}

/* a cell worth re-announcing: stairs, the amulet, or a floor item */
static int lookable(char c)
{
    return c == '>' || c == '<' || c == '"' ||
           c == ')' || c == '[' || c == '!' ||
           c == '%' || c == '?' || c == '=' || c == '/' || c == '&' ||
           c == '_' || c == '{';
}

/* Per-visit set of traps that have already been sprung (so they don't re-fire).
 * A trap cell is rendered '^' once revealed -- by springing OR by searching --
 * but '^' alone no longer means "safe": only membership here disarms it. The
 * set is per visit (reset in build_level), because the level itself regenerates
 * deterministically, re-hiding every trap. 8 slots is plenty for one level;
 * if it ever overflows the worst case is a trap that can re-fire. */
#define MAXSPRUNG 8
static uint16_t sprung[MAXSPRUNG];
static uint8_t  n_sprung;

static void traps_reset(void) { n_sprung = 0; }

static int is_sprung(uint8_t x, uint8_t y)
{
    uint16_t k = (uint16_t)y * MAPW + x;
    uint8_t i;
    for (i = 0; i < n_sprung; i++) if (sprung[i] == k) return 1;
    return 0;
}

static void add_sprung(uint8_t x, uint8_t y)
{
    if (n_sprung < MAXSPRUNG) sprung[n_sprung++] = (uint16_t)y * MAPW + x;
}

/* Deterministic per-cell trap: a side hash (never rn2, so level generation and
 * persistence stay in sync). From Dlvl 2 on, ~1/47 of floor cells hide one of
 * three traps; it springs the first time you step there. */
#define NTRAP 3
static int trap_type(uint8_t x, uint8_t y)
{
    uint16_t h;
    if (dlvl < 2) return -1;
    if (shop_in_room(x, y)) return -1;  /* NetHack rule: no traps inside a shop
                                         * (a trap door mid-shopping is cruel).
                                         * Pure rect test -- no RNG, so the
                                         * deterministic gen stays in sync. */
    h = (uint16_t)(world_seed * 31u + (uint16_t)dlvl * 2179u
                   + (uint16_t)x * 71u + (uint16_t)y * 131u);
    if ((h % 47u) != 0) return -1;
    return (int)((h >> 6) % NTRAP);
}

static void spring_trap(int t, uint8_t x, uint8_t y)
{
    if (t == 0) {                       /* trap door: you drop to the next level */
        msg("A trap door!  You fall.");
        sfx_stairs();
        dlvl++;
        build_level();
        hero_x = up_x; hero_y = up_y;
        place_pet();                     /* the dog scrambles down after you */
        return;
    }
    lvl[y][x] = '^';                     /* the trap is now sprung and visible */
    add_sprung(x, y);                    /* ...and disarmed for this visit     */
    if (t == 1) {                       /* dart */
        uint8_t d = (uint8_t)(rn2(5) + 2);
        msg("A dart hits you!");
        sfx_hurt();
        if (php <= d) { php = 0; dead = 1; }
        else        php = (uint8_t)(php - d);
    } else {                            /* sleeping gas */
        if (intrinsics & INTR_SLEEP_RES) {
            msg("A whiff of gas.  You yawn.");
        } else {
            msg("Sleeping gas!");
            st_sleep = (uint8_t)(st_sleep + rn2(4) + 3);
        }
    }
}

/* Search the eight cells around the hero, revealing (but not disarming) any
 * hidden trap as '^'. A revealed trap is NOT added to the sprung set, so it
 * still fires once if you walk onto it. */
void do_search(void) __banked
{
    int dx, dy;
    uint8_t found = 0;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int x = hero_x + dx, y = hero_y + dy;
            if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) continue;
            if (lvl[y][x] == '.' && trap_type((uint8_t)x, (uint8_t)y) >= 0) {
                lvl[y][x] = '^';
                found = 1;
            }
        }
    if (found) map_flush = 1;   /* +zx: the revealed '^' isn't the hero's cell */
    msg(found ? "You find a trap!" : "You search around.");
    turns++; acted = 1;
}

/* Spring a hidden trap if the hero has just stepped onto (nx,ny) and it hides an
 * unsprung one. Returns 1 if a trap fired (the caller then returns, since a trap
 * door may already have rebuilt the level). Shared by an ordinary step and a
 * place-swap with the pet/keeper -- displacing your dog onto a trap still
 * triggers it under you. */
static int maybe_trap(char dest, uint8_t nx, uint8_t ny)
{
    int tt;
    if ((dest != '.' && dest != '^') || is_sprung(nx, ny)) return 0;
    tt = trap_type(nx, ny);
    if (tt < 0) return 0;
    spring_trap(tt, nx, ny);
    return 1;
}

void try_move(int dx, int dy) __banked
{
    int nx, ny;
    char dest;
    int was_shop, mi;

    if (st_conf) {                  /* confused: lurch off in a random direction */
        do { dx = (int)rn2(3) - 1; dy = (int)rn2(3) - 1; } while (!dx && !dy);
    }
    nx = hero_x + dx;
    ny = hero_y + dy;
    dest = terrain(nx, ny);

    if (!walkable(dest)) {
        /* Bumping a wall: if you're standing on an item or stairs, re-announce
         * that (the @ hides the tile) rather than burying it under "It's a wall". */
        char under = terrain(hero_x, hero_y);
        if (lookable(under)) describe(under, 1);
        else                 describe(dest, 0);
        return;
    }
    mi = monster_at(nx, ny);
    if (mi >= 0) {
        if (m_type[mi] == MON_KEEPER || mi == pet_idx ||
            m_peace[mi]) {              /* swap past keeper/pet/peaceful -- a
                                         * townsman is murdered by choice
                                         * (throw/zap/cast), never by a bump */
            m_x[mi] = (uint8_t)hero_x;             /* the keeper/pet steps aside */
            m_y[mi] = (uint8_t)hero_y;             /* so you never bump into it  */
            hero_x = nx; hero_y = ny;
            turns++; acted = 1;
            maybe_trap(dest, (uint8_t)nx, (uint8_t)ny);   /* a trap under it still springs */
            return;
        }
        acted = 1;
        attack_monster((uint8_t)mi);
        return;
    }
    was_shop = shop_in_room(hero_x, hero_y);
    hero_x = nx;
    hero_y = ny;
    turns++;
    acted = 1;
    /* Stepping onto floor, or onto a known-but-still-armed trap ('^' that you
     * found by searching), can spring a hidden trap. */
    if (maybe_trap(dest, (uint8_t)nx, (uint8_t)ny)) return;
    if (dest == '$') {
        uint16_t amt = (uint16_t)(rn2(20) + 1);
        gold = (uint16_t)(gold + amt);
        level_take_gold((uint8_t)nx, (uint8_t)ny);
        msg_num("You pick up ", amt, " gold pieces.");
        sfx_gold();
    } else if (dest == '_') {
        altar_sense();              /* an altar reveals your items' BUC */
    } else if (!was_shop && shop_in_room(nx, ny)) {
        msg("Shop: , to buy, d to sell.");
    } else {
        describe(dest, 1);
    }
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

void new_game(void) __banked
{
    pmaxhp = 12;
    php = pmaxhp;
    gold = 0;
    dlvl = 1;
    turns = 0;
    dead = 0;
    has_amulet = 0;
    luckstone_taken = 0;
    cnt_kills = cnt_corpses = cnt_reads = cnt_prayers = 0;
    won = 0;
    xp = 0;
    xlvl = 1;
    nutrition = 900;
    heal_timer = 0;
    hunger_state = 0;
    st_conf = st_blind = st_sleep = st_poison = 0;
    pray_timeout = 0;
    intrinsics = 0;
    known_spells = 0;
    max_dlvl = 1;
    luck = 0;
    pick_class();                 /* who are you? (fills the sheet, hp, pw) */
    have_pet = 1; pet_hp = 8; pet_kills = 0;  /* you start with a faithful puppy */
    item_reset();
    give_kit();                   /* the class's starting gear + purse */
    level_reset_persistence();
    monster_reset_persistence();
    fov_reset();                 /* forget exploration of the old world */
    rng_seed();                  /* a brand new world */
    build_level();
    hero_x = up_x; hero_y = up_y;
    place_pet();                 /* the dog starts at your side */
    fov_update(hero_x, hero_y);
}

/* ============================================================
 * Score screen (shown on death or victory) + a persisted high score
 * ============================================================ */

#define HI_NAME  "nexthack.hi"
#define HI_MAGIC 0x4948u      /* 'H','I' */

struct hiscore { uint16_t magic, score, depth; uint8_t pclass, won; };

/* the run's score: gold, plus depth/experience, plus big bonuses for the
 * Amulet and for surfacing with it (a win). uint16, plenty for a Z80 run. */
static uint16_t run_score(uint8_t victory)
{
    uint16_t s = gold;
    s = (uint16_t)(s + max_dlvl * 50u + xp);
    if (has_amulet) s = (uint16_t)(s + 1000u);
    if (victory)    s = (uint16_t)(s + 2000u);
    return s;
}

/* A full-screen summary of the finished run + the best score so far, updated
 * in place if beaten. Shared by both targets (plain text). Dismissed with
 * Enter. */
void score_screen(uint8_t victory) __banked
{
    struct hiscore hi;
    uint16_t sc = run_score(victory);
    uint8_t  h, x, rec = 0;
    int k;

    /* read the old record (absent/garbage file -> zero) */
    h = file_open(HI_NAME);
    if (h != FILE_ERR) { file_read(h, &hi, sizeof hi); file_close(h); }
    if (h == FILE_ERR || hi.magic != HI_MAGIC) {
        hi.magic = HI_MAGIC; hi.score = 0; hi.depth = 1; hi.pclass = 0; hi.won = 0;
    }
    if (sc > hi.score) {                 /* a new record: keep it */
        hi.score = sc; hi.depth = max_dlvl; hi.pclass = pclass; hi.won = victory;
        rec = 1;
        h = file_create(HI_NAME);
        if (h != FILE_ERR) { file_write(h, &hi, sizeof hi); file_close(h); }
    }

    /* every line kept within 32 columns so the 128K bar doesn't clip it */
    tm_cls();
    print_str(2, 2, victory ? "You escaped with the Amulet!"
                            : "Here lies the adventurer.", C_WHITE | C_BRIGHT);
    x = print_str(2, 5, "A level-", C_CYAN | C_BRIGHT);
    x = put_uint(x, 5, xlvl, C_CYAN | C_BRIGHT);
    x = print_str(x, 5, " ", C_CYAN | C_BRIGHT);
    print_str(x, 5, class_name(), C_CYAN | C_BRIGHT);       /* same bank */
    x = print_str(2, 6, "Reached Dlvl ", C_CYAN | C_BRIGHT);
    put_uint(x, 6, max_dlvl, C_CYAN | C_BRIGHT);
    x = put_uint(2, 7, turns, C_CYAN | C_BRIGHT);
    x = print_str(x, 7, " turns, ", C_CYAN | C_BRIGHT);
    x = put_uint(x, 7, gold, C_CYAN | C_BRIGHT);
    print_str(x, 7, " gold", C_CYAN | C_BRIGHT);

    x = print_str(2, 9, "Score: ", C_WHITE | C_BRIGHT);
    put_uint(x, 9, sc, C_WHITE | C_BRIGHT);
    if (rec) print_str(2, 10, "A new record!", C_YELLOW | C_BRIGHT);
    else {
        x = print_str(2, 10, "Best: ", C_GREEN | C_BRIGHT);
        put_uint(x, 10, hi.score, C_GREEN | C_BRIGHT);
    }
    x = 2;
    if (cnt_kills == 0)   x = print_str(x, 11, "Pacifist  ",   C_YELLOW | C_BRIGHT);
    if (cnt_corpses == 0) print_str(x, 11, "Vegetarian",       C_YELLOW | C_BRIGHT);
    x = 2;
    if (cnt_reads == 0)   x = print_str(x, 12, "Illiterate  ", C_YELLOW | C_BRIGHT);
    if (cnt_prayers == 0) print_str(x, 12, "Atheist",          C_YELLOW | C_BRIGHT);

    print_str(2, 14, "Press Enter.", C_WHITE | C_BRIGHT);
    in_wait_nokey();
    do { k = getkey(); } while (k != 13);
    in_wait_nokey();
    map_dirty = 1;
}

/* ============================================================
 * Title screen and main loop
 * ============================================================ */

/* The seed comes from how long the player takes to press a key, which gives
 * far more variety than reading the machine state at the same instant on
 * every cold boot. */
#ifdef __ZXNEXT
/* ---- Layer 2 pixel-art screens (title + victory) -------------------------
 * Each 256x192 image lives in three banks (title 16/17/18, victory 19/20/21),
 * read in place by the Layer 2 display engine; its 9-bit palette is banked
 * alongside this code (PAGE_22). Showing one reprograms a handful of NextRegs:
 * stream the palette, point Layer 2 at the image's first bank, turn the tilemap
 * off and Layer 2 on. tm_init's tilemap setup is untouched, so hiding Layer 2
 * restores the playfield instantly. */
extern const uint8_t title_pal[];     /* 256 colours, 2 bytes each (NextReg 0x44) */
extern const uint8_t victory_pal[];

static void show_layer2(const uint8_t *pal, uint8_t bank)
{
    uint16_t i;
    ZXN_WRITE_REG(0x43, 0x10);       /* select Layer 2 palette, autoinc on      */
    ZXN_WRITE_REG(0x40, 0x00);       /* start at colour index 0                 */
    for (i = 0; i < 256; i++) {      /* stream 9-bit colours: two bytes each    */
        ZXN_WRITE_REG(0x44, pal[i * 2]);
        ZXN_WRITE_REG(0x44, pal[i * 2 + 1]);
    }
    ZXN_WRITE_REG(0x70, 0x00);       /* Layer 2 = 256x192, palette offset 0     */
    ZXN_WRITE_REG(0x12, bank);       /* Layer 2 framebuffer = image's first bank*/
    ZXN_WRITE_REG(0x6B, 0x00);       /* tilemap off (so only Layer 2 shows)     */
    ZXN_WRITE_REG(0x69, 0x80);       /* Layer 2 visible (bit 7)                 */
}

static void hide_layer2(void)
{
    ZXN_WRITE_REG(0x69, 0x00);       /* Layer 2 off                             */
    ZXN_WRITE_REG(0x6B, 0xC0);       /* tilemap back on (enable, 80x32)         */
}

void title_screen(void) __banked
{
    uint16_t s = 1;

    show_layer2(title_pal, 16);
    while (in_inkey() == 0)          /* seed from how long until the first key  */
        s += 0x9E37u;
    s ^= (uint16_t)(((uint16_t)ZXN_READ_REG(0x1F) << 8) ^ ZXN_READ_REG(0x1E));
    world_seed = s ? s : 0xACE1u;
    rng_set(world_seed);
    in_wait_nokey();
    hide_layer2();
}

/* Shown when the hero surfaces carrying the Amulet of Yendor: the hand-drawn
 * victory image (Layer 2), dismissed with Enter (back to the title to restart). */
void victory_screen(void) __banked
{
    int k;

    sfx_levelup();
    show_layer2(victory_pal, 19);
    in_wait_nokey();
    do { k = getkey(); } while (k != 13);
    in_wait_nokey();
    hide_layer2();
}
#else
/* ---- title / victory screens ---------------------------------------------
 * Plain text for now; the hand-drawn SCR loading screens are Phase 2. The
 * world seed still comes from how long the player takes to press the first key
 * (mixed with the FRAMES counter), which gives far more variety than reading
 * the machine state at the same instant on every cold boot. */
void title_screen(void) __banked
{
    uint16_t s = 1;

    show_title_scr();                /* the hand-drawn SCR loading screen */
    while (in_inkey() == 0)          /* seed from how long until the first key  */
        s += 0x9E37u;
    s ^= *(volatile uint16_t *)0x5C78u;   /* mix in the FRAMES counter */
    world_seed = s ? s : 0xACE1u;
    rng_set(world_seed);
    in_wait_nokey();
    tm_cls();
}

/* Shown when the hero surfaces carrying the Amulet of Yendor; dismissed with
 * Enter (back to the title to restart). */
void victory_screen(void) __banked
{
    int k;

    sfx_levelup();
    show_victory_scr();              /* the hand-drawn SCR win screen */
    in_wait_nokey();
    do { k = getkey(); } while (k != 13);
    in_wait_nokey();
    tm_cls();
}
#endif

/* main() lives in mainentry.c (resident): the CRT jumps straight to it, so it
 * cannot be banked. Everything it calls above is __banked (nexthack.h). */
