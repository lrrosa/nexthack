/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* nexthack.h - entry points into the game module (nexthack.c).
 *
 * nexthack.c holds the shared game-state globals (resident DATA) and the game
 * logic: rendering, the turn step, level orchestration, save/restore and the
 * screens. All of that CODE is banked (PAGE_22_CODE); only main() stays
 * resident (mainentry.c). The functions main() calls are therefore __banked,
 * reached through the trampoline. Their inner loops touch only resident leaves
 * (terrain/tile_for/FOV in level.c, the platform primitives) and resident DATA,
 * so the per-cell work pays no trampoline cost -- only the once-per-turn entry
 * call does. */
#ifndef NEXTHACK_H
#define NEXTHACK_H

void title_screen(void)   __banked;
int  load_game(void)      __banked;
int  save_game(void)      __banked;
void build_level(void)    __banked;
void new_game(void)       __banked;
void victory_screen(void) __banked;
void score_screen(uint8_t victory) __banked;  /* death/win summary + hi-score */
void draw_help(void)      __banked;
void show_help(void)      __banked;   /* '?' key screen (128K); no-op on the Next */
void draw_status(void)    __banked;
void draw_map(void)       __banked;
void upkeep(void)         __banked;
void try_move(int dx, int dy) __banked;
void go_down(void)        __banked;
void go_up(void)          __banked;
void do_pray(void)        __banked;
void do_search(void)      __banked;
void do_farlook(void)     __banked;   /* ';': inspect any visible/remembered cell */

#endif /* NEXTHACK_H */
