/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* leveltmpl.c - BANKED loader for the hand-drawn special-level templates
 * (Phase 24). The template grids + their room rects (the generated
 * leveltmpl_data.h) are const-banked in PAGE_22_CODE, so this loader -- which
 * runs with that page mapped -- reads them IN PLACE: no resident cost. For an
 * SP_TEMPLATE depth, gen_level (levelgen.c) calls load_template: it stamps the
 * map into lvl[][], records the stairs, and fills r_*[]/rcount so FOV lights
 * the chambers (a raw grid has no rooms, so without this only radius-1 +
 * corridor rays would reveal anything). */

#include "level.h"        /* lvl, rcount, up_x/up_y/dn_x/dn_y, MAPW/MAPH */

extern uint8_t r_x[], r_y[], r_w[], r_h[];   /* room rects (defined in levelgen.c) */

#ifdef __ZXNEXT
#pragma codeseg PAGE_22_CODE
#else
#pragma codeseg BANK_3
#endif
#ifdef __ZXNEXT
#pragma constseg PAGE_22_CODE
#else
#pragma constseg BANK_3
#endif
#include "leveltmpl_data.h"   /* NTMPL, tmpl_map, tmpl_nroom, tmpl_room (const) */

uint8_t template_count(void) __banked
{
    return NTMPL;
}

void load_template(uint8_t idx) __banked
{
    uint8_t x, y, r;
    if (idx >= NTMPL) idx = 0;

    for (y = 0; y < MAPH; y++)
        for (x = 0; x < MAPW; x++) {
            char c = tmpl_map[idx][y][x];
            lvl[y][x] = c;
            if (c == '<')      { up_x = x; up_y = y; }
            else if (c == '>') { dn_x = x; dn_y = y; }
        }

    rcount = tmpl_nroom[idx];
    for (r = 0; r < rcount; r++) {
        r_x[r] = tmpl_room[idx][r][0];
        r_y[r] = tmpl_room[idx][r][1];
        r_w[r] = tmpl_room[idx][r][2];
        r_h[r] = tmpl_room[idx][r][3];
    }
}
