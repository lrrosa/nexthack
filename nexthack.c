/* SPDX-License-Identifier: GPL-3.0-or-later */
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
 * Pragmatic-hybrid port: not a recompile of NetHack's C, but a fresh
 * engine on the same design, sized for the Z80N.
 * ============================================================ */

#include "game.h"
#include "platform.h"
#include "rng.h"
#include "level.h"
#include "monster.h"
#include "item.h"
#include "sfx.h"

/* ---- shared game/run state (declared extern in game.h) ---- */
int      hero_x, hero_y;
uint16_t dlvl = 1;
uint16_t turns = 0;
uint8_t  php = 12, pmaxhp = 12;
uint16_t gold = 0;
uint8_t  dead = 0;
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
 * Level orchestration
 * ============================================================ */

/* Build the current dlvl: terrain + gold, then monsters (which must see the
 * freshly placed gold), then re-apply remembered mutations. The order keeps
 * generation deterministic across revisits. */
static void build_level(void)
{
    gen_level();
    spawn_level_monsters();
    apply_gold_persistence();
    apply_monster_persistence();
    apply_item_persistence();
    /* note: FOV memory is per depth and persists across visits, so it is NOT
     * reset here - only on a new game (see new_game / main). */
}

/* ============================================================
 * Rendering
 * ============================================================ */

static void draw_map(void)
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
static void upkeep(void)
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
}

static void draw_help(void)
{
    print_str(0, 25,
        "Move: cursor or vi-keys (h j k l + y u b n)    Stairs: > < Enter    Wait: s",
        C_CYAN | C_BRIGHT);
    print_str(0, 26,
        "Cmd: , get  i inv  w wield  W wear  P ring  q quaff  e eat  r read",
        C_CYAN | C_BRIGHT);
}

static void draw_status(void)
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
    case '%': msg("There is a food ration here.  (, to pick up)");   break;
    case ')': msg("There is a weapon here.  (, to pick up)");        break;
    case '[': msg("There is some armor here.  (, to pick up)");      break;
    case '!': msg("There is a potion here.  (, to pick up)");        break;
    case '?': msg("There is a scroll here.  (, to pick up)");        break;
    case '=': msg("There is a ring here.  (, to pick up)");          break;
    default:  msg("");                                break;
    }
}

static void try_move(int dx, int dy)
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

static void go_down(void)
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

static void go_up(void)
{
    if (terrain(hero_x, hero_y) == '<') {
        if (dlvl > 1) {
            dlvl--;
            turns++;
            build_level();
            hero_x = dn_x; hero_y = dn_y; /* arrive on the new down-stairs */
            msg("You climb up the stairs.");
            sfx_stairs();
        } else {
            msg("You can't go up from here.");
        }
    } else {
        msg("You can't go up here.");
    }
}

static void new_game(void)
{
    pmaxhp = 12;
    php = pmaxhp;
    gold = 0;
    dlvl = 1;
    turns = 0;
    dead = 0;
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
static void title_screen(void)
{
    uint16_t s = 1;

    tm_cls();
    print_str(36,  8, "NextHack", C_YELLOW | C_BRIGHT);
    print_str(22, 10, "A roguelike for the ZX Spectrum Next", C_CYAN | C_BRIGHT);
    print_str(27, 14, "Press any key to begin...", C_WHITE | C_BRIGHT);

    while (in_inkey() == 0)
        s += 0x9E37u;
    s ^= (uint16_t)(((uint16_t)ZXN_READ_REG(0x1F) << 8) ^ ZXN_READ_REG(0x1E));
    world_seed = s ? s : 0xACE1u;
    rng_set(world_seed);
    in_wait_nokey();
}

void main(void)
{
    int k;

    zx_border(C_BLACK);
    tm_init();

    title_screen();
    item_reset();
    fov_reset();
    build_level();
    hero_x = up_x; hero_y = up_y;
    fov_update(hero_x, hero_y);

    tm_cls();
    draw_help();
    draw_status();
    draw_map();
    msg("Welcome to NextHack!");

    for (;;) {
        k = getkey();

        acted = 0;
        switch (k) {
        /* movement: cursor keys + NetHack vi-keys (hjkl + yubn diagonals) */
        case 'h': case  8: try_move(-1,  0); break;   /* left       */
        case 'l': case  9: try_move(+1,  0); break;   /* right      */
        case 'j': case 10: try_move( 0, +1); break;   /* down       */
        case 'k': case 11: try_move( 0, -1); break;   /* up         */
        case 'y': try_move(-1, -1); break;            /* up-left    */
        case 'u': try_move(+1, -1); break;            /* up-right   */
        case 'b': try_move(-1, +1); break;            /* down-left  */
        case 'n': try_move(+1, +1); break;            /* down-right */

        /* NetHack-style commands (debounced: one press = one action) */
        case ',': do_pickup();      turns++; acted = 1; in_wait_nokey(); break;
        case 'w': do_wield();       turns++; acted = 1; in_wait_nokey(); break;
        case 'W': do_wear();        turns++; acted = 1; in_wait_nokey(); break;
        case 'P': do_puton();       turns++; acted = 1; in_wait_nokey(); break;
        case 'q': do_quaff();       turns++; acted = 1; in_wait_nokey(); break;
        case 'e': do_eat();         turns++; acted = 1; in_wait_nokey(); break;
        case 'r': do_read();        turns++; acted = 1; in_wait_nokey(); break;
        case 'i': show_inventory(); break;          /* viewing costs no turn */

        /* search / wait */
        case 's':
        case '.':
        case ' ': turns++; acted = 1; msg("You wait."); break;

        /* stairs: '>'/'<' or Enter for whichever you stand on */
        case '>': go_down(); in_wait_nokey(); break;
        case '<': go_up();   in_wait_nokey(); break;
        case 13:
            if (terrain(hero_x, hero_y) == '>')      go_down();
            else if (terrain(hero_x, hero_y) == '<') go_up();
            else msg("There are no stairs here.");
            in_wait_nokey();
            break;
        default:  break;
        }

        if (acted && !dead) {
            upkeep();               /* hunger ticks, HP regenerates  */
            if (!dead)
                monsters_turn();    /* monsters chase and attack     */
        }

        fov_update(hero_x, hero_y); /* recompute what the hero can see */
        draw_status();
        draw_map();

        if (dead) {
            sfx_die();
            msg("You die...   Press Enter to start over.");
            in_wait_nokey();
            do { k = getkey(); } while (k != 13);
            new_game();
            draw_status();
            draw_map();
            msg("You feel much better.  A new adventure begins!");
            in_wait_nokey();
        }

        in_pause(40);   /* throttle continuous (held-key) movement */
    }
}
