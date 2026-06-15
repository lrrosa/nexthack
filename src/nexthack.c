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
uint8_t  weapon_dmg = 0;
uint8_t  armor_def = 0;
uint8_t  ac = 10;
uint16_t xp = 0;
uint8_t  xlvl = 1;
int16_t  nutrition = 900;

/* hunger/regeneration bookkeeping */
static uint8_t heal_timer = 0;
static uint8_t hunger_state = 0;   /* 0 ok  1 hungry  2 weak  3 fainting */

/* ============================================================
 * Save / restore (NetHack-style: save & quit, consumed on load)
 * ============================================================ */

#define SAVE_NAME  "nexthack.sav"
#define SAVE_MAGIC 0x484Eu          /* 'N','H' */
#define SAVE_VER   10     /* fix: special levels no longer inherit the last level's shop/vault */

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
};

/* From here down, all of nexthack.c's CODE is banked into PAGE_22_CODE (mapped
 * into the 0xC000 window on demand). The globals above are DATA and stay
 * resident. The functions main() calls are __banked (see nexthack.h); the
 * static helpers (hunger_*, describe) are reached by in-page calls. */
#pragma codeseg PAGE_22_CODE

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
    /* note: FOV memory is per depth and persists across visits, so it is NOT
     * reset here - only on a new game (see new_game / main). */
}

/* ============================================================
 * Rendering
 * ============================================================ */

void draw_map(void) __banked
{
    const uint8_t *seen = fov_bitmap();   /* explored bitmap        */
    const uint8_t *vis  = vis_bitmap();   /* visible-this-turn map  */
    uint16_t idx = 0;
    uint8_t x, y, t, attr;
    int mi;

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
            *p++ = t;
            *p++ = attr;
        }
    }
}

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
        if (hs == 1)      msg("You are beginning to feel hungry.");
        else if (hs == 2) msg("You are getting weak from hunger.");
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

    maybe_spawn_wanderer();                     /* the dungeon refills over time */
}

void draw_help(void) __banked
{
    print_str(0, 25,
        "Move: cursor or vi-keys (h j k l + y u b n)    Stairs: > < Enter    Wait: s",
        C_CYAN | C_BRIGHT);
    print_str(0, 26,
        "Cmd: ,get  i inv  d sell  w wield  W wear  P ring  q quaff e eat r read  S save",
        C_CYAN | C_BRIGHT);
}

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
    while (x < 80) putcell(x++, 23, ' ', C_GREEN);
}

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
    case '"': msg("The Amulet of Yendor lies here!  (, to pick up)"); break;
    case ')': case '[': case '!': case '%': case '?': case '=':
        msg2("You see here ", floor_item_desc(), ".  (, to pick up)"); break;
    default:  msg("");                                break;
    }
}

void try_move(int dx, int dy) __banked
{
    int nx = hero_x + dx;
    int ny = hero_y + dy;
    char dest = terrain(nx, ny);
    int mi;

    if (!walkable(dest)) {
        describe(dest, 0);
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
    hero_x = nx;
    hero_y = ny;
    turns++;
    acted = 1;
    if (dest == '$') {
        uint16_t amt = (uint16_t)(rn2(20) + 1);
        gold = (uint16_t)(gold + amt);
        level_take_gold((uint8_t)nx, (uint8_t)ny);
        msg_num("You pick up ", amt, " gold pieces.");
        sfx_gold();
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

/* main() lives in mainentry.c (resident): the CRT jumps straight to it, so it
 * cannot be banked. Everything it calls above is __banked (nexthack.h). */
