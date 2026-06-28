/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* mainentry.c - the resident entry point and turn loop.
 *
 * Everything the loop calls lives in banked modules (nexthack.c game logic in
 * PAGE_22, item.c/levelgen.c in PAGE_20); main() itself must stay resident
 * because the CRT jumps straight to it. It is a thin dispatcher: read a key,
 * call the (banked) action, then the (banked) per-turn step and redraw. The
 * shared game-state globals live in nexthack.c (resident DATA), reached here
 * via game.h. */

#include "game.h"
#include "platform.h"
#include "rng.h"
#include "level.h"
#include "monster.h"
#include "item.h"
#include "sfx.h"
#include "nexthack.h"

void main(void)
{
    int k;

    zx_border(C_BLACK);
    tm_init();

start_game:
    title_screen();
    if (load_game()) {              /* a saved game was found: resume it */
        build_level();              /* regenerate the saved depth        */
        place_pet();                /* and bring the dog back to your side */
        fov_update(hero_x, hero_y);
        tm_cls();
        draw_help();
        draw_status();
        draw_map();
        msg("Game restored.  Welcome back!");
    } else {                        /* no save: start a fresh adventure  */
        item_reset();
        fov_reset();
        have_pet = 1; pet_hp = 8;   /* the first adventure begins with a dog */
        build_level();
        hero_x = up_x; hero_y = up_y;
        place_pet();
        fov_update(hero_x, hero_y);
        tm_cls();
        draw_help();
        draw_status();
        draw_map();
        msg("Welcome to NextHack!");
    }

    for (;;) {
        acted = 0;
        if (st_sleep) { acted = 1; turns++; }   /* asleep/paralysed: forfeit the turn */
        else k = getkey();

        if (!st_sleep) switch (k) {
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
        case 'q': do_quaff();       in_wait_nokey(); break;   /* set acted/turns */
        case 'e': do_eat();         in_wait_nokey(); break;   /* themselves, so a */
        case 'r': do_read();        in_wait_nokey(); break;   /* cancel costs none */
        case 't': do_throw();       in_wait_nokey(); break;   /* throw a weapon   */
        case 'i': show_inventory(); break;          /* viewing costs no turn */
        case '?': show_help();      break;          /* full key-list screen; no turn */
        case 'd': do_sell(); in_wait_nokey(); break; /* sell in a shop; no turn */
        case 'E':                                   /* engrave Elbereth in the dust */
            el_x = (uint8_t)hero_x; el_y = (uint8_t)hero_y; el_life = 30;
            msg("You engrave Elbereth in the dust.");
            turns++; acted = 1; in_wait_nokey(); break;
        case 'p': do_pray(); in_wait_nokey(); break;  /* pray to your god */

        case 'S':                                   /* save game and quit to title */
            if (save_game()) {
                tm_cls();
#ifdef __ZXNEXT
                print_str(23, 10, "Game saved.  You may switch off.", C_WHITE | C_BRIGHT);
                print_str(24, 13, "Press Enter to return to the title.", C_CYAN | C_BRIGHT);
#else
                print_str(10, 10, "Game saved.", C_WHITE | C_BRIGHT);
                print_str(5, 13, "Enter returns to title.", C_CYAN | C_BRIGHT);
#endif
                in_wait_nokey();
                do { k = getkey(); } while (k != 13);
                in_wait_nokey();
                goto start_game;
            }
            msg("Save failed (SD writable?)");
            in_wait_nokey();
            break;

        case 's': do_search(); in_wait_nokey(); break;  /* search for nearby traps */

        /* wait a turn */
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

        if (won) {
            victory_screen();
            new_game();
            tm_cls();
            draw_help();
            draw_status();
            draw_map();
            msg("A new adventure begins!");
            in_wait_nokey();
        } else if (dead) {
            sfx_die();
            msg("You die...  Enter to restart.");
            in_wait_nokey();
            do { k = getkey(); } while (k != 13);
            new_game();
            draw_status();
            draw_map();
            msg("You feel better.  New game!");
            in_wait_nokey();
        }

#ifdef __ZXNEXT
        in_pause(40);   /* throttle continuous (held-key) movement */
#else
        in_pause(6);    /* 128K: light throttle (was 40). Below ~6 ms the per-
                         * turn work -- mainly the monster-chase BFS -- is the
                         * floor anyway, so further cuts saturate. in_pause(0)
                         * is avoided: the lib's msec countdown underflows to a
                         * ~65 s pause. */
#endif
    }
}
