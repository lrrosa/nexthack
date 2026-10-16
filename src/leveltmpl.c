/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* leveltmpl.c - BANKED loader for the hand-drawn special-level templates
 * (Phase 24). The template grids + their room rects (the generated
 * leveltmpl_data.h) are const-banked in this module's own bank, so this loader
 * -- which runs with that page mapped -- reads them IN PLACE: no resident cost.
 * They are LZ-PACKED (tools/lztmpl.py): 8400 B of grids in 864 B of bank,
 * because the thing they unpack into, lvl[][], already existed.
 * For an SP_TEMPLATE depth, gen_level (levelgen.c) calls load_template: it
 * unpacks the map into lvl[][], records the stairs, and fills r_*[]/rcount so
 * FOV lights the chambers (a raw grid has no rooms, so without this only
 * radius-1 + corridor rays would reveal anything). */

#include "level.h"        /* lvl, rcount, up_x/up_y/dn_x/dn_y, MAPW/MAPH */
#include "platform.h"     /* lz_expand (resident since 1.8) */

extern uint8_t r_x[], r_y[], r_w[], r_h[];   /* room rects (defined in levelgen.c) */

/* The loader and its packed templates are banked TOGETHER (it unpacks them
 * from that bank), and history says give them room: they used to share a bank
 * with code that kept growing, the combined section silently overflowed 16 KB
 * -- neither the linker nor the tape packer errored -- and the bank was
 * truncated at its edge, so the clipped data crashed the machine on entering
 * any template level (mktap128.py now refuses an oversized bank). banks.json
 * says which bank each target uses, and why. */
#include "leveltmpl_data.h"   /* NTMPL, tmpl_lz, tmpl_off, tmpl_nroom, tmpl_room */

uint8_t template_count(void) __banked
{
    return NTMPL;
}

/* The grids unpack straight into lvl[][] with lz_expand -- resident in
 * platform.c since 1.8, when the 128K's SCR screens took up the same format:
 * it reads its stream from whatever bank its caller runs in, so the template
 * data stays here beside this loader. */

void load_template(uint8_t idx) __banked
{
    uint8_t x, y, r;
    if (idx >= NTMPL) idx = 0;

    lz_expand(tmpl_lz + tmpl_off[idx], &lvl[0][0], (uint16_t)MAPW * MAPH);

    for (y = 0; y < MAPH; y++)
        for (x = 0; x < MAPW; x++) {
            char c = lvl[y][x];
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
