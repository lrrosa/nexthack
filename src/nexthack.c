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
uint8_t  won = 0;
uint8_t  acted = 0;
uint8_t  map_dirty = 1;   /* +zx renderer flag (unused on Next) */
uint8_t  weapon_dmg = 0;
uint8_t  armor_def = 0;
uint8_t  ac = 10;
uint16_t xp = 0;
uint8_t  xlvl = 1;
int16_t  nutrition = 900;

/* transient status effects (see game.h): per-turn countdowns, 0 = inactive */
uint8_t  st_conf = 0, st_blind = 0, st_sleep = 0, st_poison = 0;

/* hunger/regeneration bookkeeping */
static uint8_t heal_timer = 0;
static uint8_t hunger_state = 0;   /* 0 ok  1 hungry  2 weak  3 fainting */

/* ============================================================
 * Save / restore (NetHack-style: save & quit, consumed on load)
 * ============================================================ */

#define SAVE_NAME  "nexthack.sav"
#define SAVE_MAGIC 0x484Eu          /* 'N','H' */
#define SAVE_VER   15     /* v1.3.0 save format (FOV_SLOTS 8->6, + new feature state) */

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
};

/* From here down, all of nexthack.c's CODE is banked into PAGE_22_CODE (mapped
 * into the 0xC000 window on demand). The globals above are DATA and stay
 * resident. The functions main() calls are __banked (see nexthack.h); the
 * static helpers (hunger_*, describe) are reached by in-page calls. */
#ifdef __ZXNEXT
#pragma codeseg PAGE_22_CODE
#else
#pragma codeseg BANK_3
#endif

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
void build_level(void) __banked
{
    gen_level();
    spawn_level_monsters();
    { uint8_t kx, ky; if (shop_keeper_xy(&kx, &ky)) place_shopkeeper(kx, ky); }
    apply_gold_persistence();
    apply_monster_persistence();
    apply_item_persistence();
    map_dirty = 1;       /* +zx: next draw_map recenters (no-op on Next) */
    /* note: FOV memory is per depth and persists across visits, so it is NOT
     * reset here - only on a new game (see new_game / main). */
}

/* ============================================================
 * Rendering
 * ============================================================ */

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
            } else if (!(seen[byte] & mask)) {
                t = T_ROCK;                       /* never seen -> dark   */
            } else if (vis[byte] & mask) {        /* in sight -> full     */
                mi = monster_at(x, y);
                t = (mi >= 0) ? mon_tile(m_type[mi]) : tile_for(lvl[y][x]);
            } else {                              /* remembered -> dim    */
                t = tile_for(lvl[y][x]);
                attr = 0x10;                      /* palette offset 1     */
            }
            if (has_shop && t == T_WALL &&        /* shop walls: warm bricks */
                x >= sx && x <= sx1 && y >= sy && y <= sy1)
                t = T_SHOPWALL;
            *p++ = t;
            *p++ = attr;
        }
    }
}
#else
#define VIEW_EDGE   6
#define VIEW_SHADOW ((uint8_t *)0x6000u)
static uint8_t vx_origin;
void draw_map(void) __banked
{
    const uint8_t *seen = fov_bitmap();   /* explored bitmap        */
    const uint8_t *vis  = vis_bitmap();   /* visible-this-turn map  */
    uint8_t *shad = VIEW_SHADOW;
    uint8_t sc, x, y, t, attr, vx, full;
    int mi, hsc, nvx;
    /* the shop room's bounds (read once), so its walls render in warm bricks */
    uint8_t sx, sy, sw, sh, sx1 = 0, sy1 = 0;
    int has_shop = shop_rect(&sx, &sy, &sw, &sh);
    if (has_shop) { sx1 = (uint8_t)(sx + sw - 1); sy1 = (uint8_t)(sy + sh - 1); }

    /* keep the viewport origin unless the hero left the central band (or a full
     * redraw is already pending), then recenter on the hero. A change forces a
     * full redraw; otherwise only changed cells are written (diff vs shad[]). */
    full = map_dirty;
    if (map_dirty) {
        nvx = hero_x - TM_W / 2;
        map_dirty = 0;
    } else {
        hsc = hero_x - (int)vx_origin;
        nvx = (hsc >= VIEW_EDGE && hsc <= TM_W - 1 - VIEW_EDGE)
              ? (int)vx_origin : hero_x - TM_W / 2;
    }
    if (nvx < 0) nvx = 0;
    if (nvx > MAPW - TM_W) nvx = MAPW - TM_W;
    if ((uint8_t)nvx != vx_origin) { vx_origin = (uint8_t)nvx; full = 1; }
    vx = vx_origin;

    for (y = 0; y < MAPH; y++) {
        uint16_t idx = (uint16_t)y * MAPW + vx;
        for (sc = 0; sc < TM_W; sc++, idx++) {
            uint8_t byte = (uint8_t)(idx >> 3);
            uint8_t mask = (uint8_t)(1u << (idx & 7));
            uint16_t si;
            x = (uint8_t)(vx + sc);
            if (x == (uint8_t)hero_x && y == (uint8_t)hero_y) {
                t = T_HERO;
                attr = (uint8_t)(udg_ink[T_HERO - T_ROCK] | 0x40);
            } else if (!(seen[byte] & mask)) {
                t = T_ROCK; attr = 0;             /* never seen -> black  */
            } else if (vis[byte] & mask) {        /* in sight -> bright   */
                mi = monster_at(x, y);
                t = (mi >= 0) ? mon_tile(m_type[mi]) : tile_for(lvl[y][x]);
                if (has_shop && t == T_WALL &&
                    x >= sx && x <= sx1 && y >= sy && y <= sy1) t = T_SHOPWALL;
                attr = (uint8_t)(udg_ink[t - T_ROCK] | 0x40);
            } else {                              /* remembered -> dim    */
                t = tile_for(lvl[y][x]);
                if (has_shop && t == T_WALL &&
                    x >= sx && x <= sx1 && y >= sy && y <= sy1) t = T_SHOPWALL;
                attr = udg_ink[t - T_ROCK];       /* BRIGHT off = dimmed  */
            }
            si = (uint16_t)(((uint16_t)y * TM_W + sc) << 1);
            if (full || shad[si] != t || shad[si + 1] != attr) {
                puttile_attr(sc, (uint8_t)(OY + y), t, attr);
                shad[si] = t; shad[si + 1] = attr;
            }
        }
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
    } else if (++heal_timer >= 20) {            /* slow regeneration */
        heal_timer = 0;
        if (php < pmaxhp) php++;
    }

    /* transient status effects tick down; poison gnaws a hit point each turn */
    if (st_poison) {
        if (php > 0) php--;
        if (php == 0) dead = 1;
        if (!--st_poison && !dead) msg("The poison wears off.");
    }
    if (st_conf  && !--st_conf)  msg("Your head clears.");
    if (st_blind && !--st_blind) { msg("You can see again."); map_dirty = 1; }
    if (st_sleep && !--st_sleep) msg("You wake up.");

    maybe_spawn_wanderer();                     /* the dungeon refills over time */
}

#ifdef __ZXNEXT
void draw_help(void) __banked
{
    print_str(0, 25,
        "Move: cursor or vi-keys (h j k l + y u b n)    Stairs: > < Enter    Wait: s",
        C_CYAN | C_BRIGHT);
    print_str(0, 26,
        "Cmd: ,get  i inv  d sell  w wield  W wear  P ring  q quaff e eat r read  S save",
        C_CYAN | C_BRIGHT);
}
#else
void draw_help(void) __banked
{
    /* The 24-row ULA has no room for a persistent help bar (row 0 = message,
     * rows 1-21 = map, rows 22-23 = status), so the keys live on a '?' screen
     * (show_help) instead. */
}
#endif

#ifdef __ZXNEXT
void show_help(void) __banked { }    /* the help bar is always on screen */
#else
/* '?' on the 128K: the key list is not visible during play, so show it on a
 * full screen, then restore the map. */
void show_help(void) __banked
{
    tm_cls();
    print_str(8,  1, "NextHack  Keys",       C_WHITE | C_BRIGHT);
    print_str(2,  3, "Move: arrow keys, or", C_CYAN | C_BRIGHT);
    print_str(7,  4, "h j k l  y u b n",     C_CYAN | C_BRIGHT);
    print_str(2,  5, "Stairs: > < or Enter", C_CYAN | C_BRIGHT);
    print_str(2,  6, "Wait: s",              C_CYAN | C_BRIGHT);
    print_str(2,  8, ", pick up",            C_CYAN | C_BRIGHT);
    print_str(2,  9, "i inventory",          C_CYAN | C_BRIGHT);
    print_str(2, 10, "w wield    W wear",    C_CYAN | C_BRIGHT);
    print_str(2, 11, "P put on ring",        C_CYAN | C_BRIGHT);
    print_str(2, 12, "q quaff    e eat",     C_CYAN | C_BRIGHT);
    print_str(2, 13, "r read     d sell",    C_CYAN | C_BRIGHT);
    print_str(2, 14, "S save and quit",      C_CYAN | C_BRIGHT);
    print_str(2, 15, "? this help",          C_CYAN | C_BRIGHT);
    print_str(4, 18, "Press any key...",     C_WHITE | C_BRIGHT);
    in_wait_nokey();
    getkey();
    in_wait_nokey();
    map_dirty = 1;
    draw_status();
    draw_map();
}
#endif

#ifdef __ZXNEXT
void draw_status(void) __banked
{
    uint8_t x;
    const char *h = hunger_label();

    /* Each status cell is written exactly once (no clear-then-fill) so the
     * status bar does not flicker as values change. */
    print_str(0, 22,
        "Player the Tourist      St:14 Dx:11 Co:14 In:10 Wi:8 Ch:10   Lawful",
        C_GREEN | C_BRIGHT);
    putcell(67, 22, ' ', C_GREEN);
    x = print_str(68, 22, h, hunger_color());      /* hunger state at the tail */
    while (x < 80) putcell(x++, 22, ' ', C_GREEN);

    x = print_str(0, 23, "Dlvl:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, dlvl, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  ", C_GREEN | C_BRIGHT);
    puttile(x, 23, T_DOLLAR); x++;        /* green '$' tile (ROM '$' is blank) */
    x = print_str(x, 23, ":", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, gold, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "  HP:", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, php, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, "(", C_GREEN | C_BRIGHT);
    x = put_uint(x, 23, pmaxhp, C_GREEN | C_BRIGHT);
    x = print_str(x, 23, ")  Pw:2(2)  AC:", C_GREEN | C_BRIGHT);
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

    x = sd_str(0, 22, "Dlvl:", C_GREEN | C_BRIGHT);
    x = sd_uint(x, 22, dlvl, C_GREEN | C_BRIGHT);
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
    case ')': case '[': case '!': case '%': case '?': case '=':
        msg2(floor_item_desc(),
             shop_in_room(hero_x, hero_y) ? " (,buy)" : " (,get)", "");
        break;
    default:  msg("");                                break;
    }
}

/* a cell worth re-announcing: stairs, the amulet, or a floor item */
static int lookable(char c)
{
    return c == '>' || c == '<' || c == '"' ||
           c == ')' || c == '[' || c == '!' ||
           c == '%' || c == '?' || c == '=';
}

/* Deterministic per-cell trap: a side hash (never rn2, so level generation and
 * persistence stay in sync). From Dlvl 2 on, ~1/47 of floor cells hide one of
 * three traps; it springs the first time you step there. */
#define NTRAP 3
static int trap_type(uint8_t x, uint8_t y)
{
    uint16_t h;
    if (dlvl < 2) return -1;
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
        return;
    }
    lvl[y][x] = '^';                     /* the trap is now sprung and visible */
    if (t == 1) {                       /* dart */
        uint8_t d = (uint8_t)(rn2(5) + 2);
        msg("A dart hits you!");
        sfx_hurt();
        if (php <= d) { php = 0; dead = 1; }
        else        php = (uint8_t)(php - d);
    } else {                            /* sleeping gas */
        msg("Sleeping gas!");
        st_sleep = (uint8_t)(st_sleep + rn2(4) + 3);
    }
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
        if (m_type[mi] == MON_KEEPER) {            /* swap past, don't attack */
            m_x[mi] = (uint8_t)hero_x;             /* the keeper steps aside   */
            m_y[mi] = (uint8_t)hero_y;             /* so it can never trap you */
            hero_x = nx; hero_y = ny;
            turns++; acted = 1;
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
    if (dest == '.') {                  /* stepping onto floor -- a hidden trap? */
        int tt = trap_type((uint8_t)nx, (uint8_t)ny);
        if (tt >= 0) { spring_trap(tt, (uint8_t)nx, (uint8_t)ny); return; }
    }
    if (dest == '$') {
        uint16_t amt = (uint16_t)(rn2(20) + 1);
        gold = (uint16_t)(gold + amt);
        level_take_gold((uint8_t)nx, (uint8_t)ny);
        msg_num("You pick up ", amt, " gold pieces.");
        sfx_gold();
    } else if (!was_shop && shop_in_room(nx, ny)) {
        msg("Shop: , to buy, d to sell.");
    } else {
        describe(dest, 1);
    }
}

void go_down(void) __banked
{
    if (terrain(hero_x, hero_y) == '>') {
        dlvl++;
        turns++;
        build_level();
        hero_x = up_x; hero_y = up_y;     /* arrive on the new up-stairs */
        msg("You descend the stairs.");
        sfx_stairs();
    } else {
        msg("You can't go down here.");
    }
}

void go_up(void) __banked
{
    if (terrain(hero_x, hero_y) == '<') {
        if (dlvl > 1) {
            dlvl--;
            turns++;
            build_level();
            hero_x = dn_x; hero_y = dn_y; /* arrive on the new down-stairs */
            msg("You climb up the stairs.");
            sfx_stairs();
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
    won = 0;
    xp = 0;
    xlvl = 1;
    nutrition = 900;
    heal_timer = 0;
    hunger_state = 0;
    st_conf = st_blind = st_sleep = st_poison = 0;
    item_reset();
    level_reset_persistence();
    monster_reset_persistence();
    fov_reset();                 /* forget exploration of the old world */
    rng_seed();                  /* a brand new world */
    build_level();
    hero_x = up_x; hero_y = up_y;
    fov_update(hero_x, hero_y);
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
