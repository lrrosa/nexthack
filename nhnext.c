/* ============================================================
 * NetHack Next - a roguelike for the ZX Spectrum Next
 * ------------------------------------------------------------
 * This module is the game itself: shared player/run state, the main
 * loop, rendering of the map and status bar, and the title screen.
 *
 * The codebase is split into modules:
 *   platform.c  - ZX Next hardware (tilemap, tiles, text, keyboard)
 *   rng.c       - random number generator
 *   level.c     - terrain, procedural generation, persistence
 *   monster.c   - monsters, chase AI, combat
 *   nhnext.c    - this file: game state, loop, rendering, title
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
    fov_reset();                /* a freshly entered level starts unexplored */
}

/* ============================================================
 * Rendering
 * ============================================================ */

static void draw_map(void)
{
    uint8_t x, y, t;
    int mi;

    /* Single pass: every cell is written once with its final tile.
     * Fog of war: unexplored cells are black; remembered terrain/items show;
     * monsters only show while currently in view. */
    for (y = 0; y < MAPH; y++) {
        for (x = 0; x < MAPW; x++) {
            if (x == (uint8_t)hero_x && y == (uint8_t)hero_y) {
                t = T_HERO;
            } else if (!fov_seen(x, y)) {
                t = T_ROCK;                       /* never seen -> dark */
            } else {
                mi = monster_at(x, y);
                if (mi >= 0 && fov_visible(x, y))
                    t = (uint8_t)(m_type[mi] == 'd' ? T_DOG : T_RAT);
                else
                    t = tile_for(lvl[y][x]);      /* remembered terrain */
            }
            puttile((uint8_t)(OX + x), (uint8_t)(OY + y), t);
        }
    }
}

static void draw_help(void)
{
    print_str(0, 25,
        "Move: WASD/arrows   Diag: QEZC   Enter: stairs   . wait",
        C_CYAN | C_BRIGHT);
    print_str(0, 26,
        "Items: , get  i inv  f wield  r wear  p quaff  g eat",
        C_CYAN | C_BRIGHT);
}

static void draw_status(void)
{
    uint8_t x;
    print_str(0, 22,
        "Player the Tourist      St:14 Dx:11 Co:14 In:10 Wi:8 Ch:10   Lawful",
        C_GREEN | C_BRIGHT);

    clear_line(23, C_GREEN);
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
    x = print_str(x, 23, "  Xp:1/0  T:", C_GREEN | C_BRIGHT);
    put_uint(x, 23, turns, C_GREEN | C_BRIGHT);
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
        } else {
            msg("You can't go up from here.");
        }
    } else {
        msg("You can't go up here.");
    }
}

static void new_game(void)
{
    php = pmaxhp;
    gold = 0;
    dlvl = 1;
    turns = 0;
    dead = 0;
    item_reset();
    level_reset_persistence();
    monster_reset_persistence();
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
    print_str(34,  8, "NetHack Next", C_YELLOW | C_BRIGHT);
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
    build_level();
    hero_x = up_x; hero_y = up_y;
    fov_update(hero_x, hero_y);

    tm_cls();
    draw_help();
    draw_status();
    draw_map();
    msg("Welcome to NetHack Next!");

    for (;;) {
        k = getkey();
        if (k >= 'A' && k <= 'Z')
            k += 32;                 /* normalise letters */

        acted = 0;
        switch (k) {
        /* orthogonal: WASD, vi hjkl, cursor keys (8/9/10/11) */
        case 'w': case 'k': case 11: try_move( 0, -1); break;
        case 's': case 'j': case 10: try_move( 0, +1); break;
        case 'a': case 'h': case  8: try_move(-1,  0); break;
        case 'd': case 'l': case  9: try_move(+1,  0); break;
        /* diagonals: QEZC and vi yubn */
        case 'q': case 'y': try_move(-1, -1); break;
        case 'e': case 'u': try_move(+1, -1); break;
        case 'z': case 'b': try_move(-1, +1); break;
        case 'c': case 'n': try_move(+1, +1); break;
        /* Enter: use whichever stair you stand on */
        case 13:
            if (terrain(hero_x, hero_y) == '>')      go_down();
            else if (terrain(hero_x, hero_y) == '<') go_up();
            else msg("There are no stairs here.");
            in_wait_nokey();          /* don't repeat-trigger stairs */
            break;
        /* items (debounced so one press = one action) */
        case ',': do_pickup(); turns++; acted = 1; in_wait_nokey(); break;
        case 'f': do_wield();  turns++; acted = 1; in_wait_nokey(); break;
        case 'r': do_wear();   turns++; acted = 1; in_wait_nokey(); break;
        case 'p': do_quaff();  turns++; acted = 1; in_wait_nokey(); break;
        case 'g': do_eat();    turns++; acted = 1; in_wait_nokey(); break;
        case 'i': show_inventory(); break;   /* viewing costs no turn */
        /* wait */
        case '.':
        case ' ': turns++; acted = 1; msg("You wait."); break;
        default:  break;
        }

        if (acted && !dead)
            monsters_turn();        /* monsters chase and attack */

        fov_update(hero_x, hero_y); /* recompute what the hero can see */
        draw_status();
        draw_map();

        if (dead) {
            msg("You die...   Press Enter to start over.");
            in_wait_nokey();
            do { k = getkey(); } while (k != 13);
            new_game();
            draw_status();
            draw_map();
            msg("You feel much better.  A new adventure begins!");
            in_wait_nokey();
        }

        in_pause(70);   /* throttle continuous (held-key) movement */
    }
}
