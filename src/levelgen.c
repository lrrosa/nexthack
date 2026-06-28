/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* levelgen.c - COLD half of the level module: procedural dungeon generation
 * and per-level gold/item persistence. Split out of level.c so it can be a
 * whole BANKED module (PAGE_20_CODE), mapped into the 0xC000 window on demand.
 *
 * Its code is cold (runs on level entry / pickup, never per frame), so the
 * trampoline cost is irrelevant. Its DATA stays resident (only code is banked),
 * so the room table r_*[] and the persistence masks gold_taken/item_taken are
 * shared with the HOT half (level.c: FOV reads the rooms, save/load the masks)
 * via plain extern. Public entry points are __banked (see level.h); the static
 * helpers are reached only by in-page calls. */

#include "level.h"
#include "rng.h"          /* rng_set, rn2, world_seed  */
#include "game.h"         /* dlvl, MAXLVL, DLVL_AMULET, has_amulet */

#ifdef __ZXNEXT
#pragma codeseg PAGE_20_CODE
#else
#pragma codeseg BANK_1
#endif

/* playable area (leaves a rock margin around the edges) */
#define PX0 1
#define PY0 1
#define PX1 78
#define PY1 19

#define SECT_COLS 4
#define SECT_ROWS 2
#define MAXROOMS  (SECT_COLS * SECT_ROWS)

/* Shared with the hot half (level.c). DATA is always resident, so these stay
 * reachable from FOV (rooms) and save/load (masks) even though our code is
 * banked. Defined here; level.c declares them extern. */
uint8_t r_x[MAXROOMS], r_y[MAXROOMS], r_w[MAXROOMS], r_h[MAXROOMS];
uint8_t gold_taken[MAXLVL + 1];    /* bit i: gold pile i collected */
uint8_t item_taken[MAXLVL + 1];    /* bit i: item i picked up      */

/* gold/item positions are generation-private (only cold code touches them) */
static uint8_t gcount;
static uint8_t g_x[8], g_y[8];
static uint8_t icount;
static uint8_t i_x[8], i_y[8];

/* Shop: index of the room that is a shop on the current level, or -1, plus the
 * reserved shopkeeper cell. Set by gen_level each time; read by shop_in_room /
 * shop_keeper_xy. Resident (3 B). */
static int8_t  shop_room = -1;
static uint8_t keeper_x, keeper_y;

/* Treasure vault (Phase 23): index of the room sealed as a vault on the current
 * level, or -1. Set by gen_level; read by level_vault_room so the monster
 * spawner can post tough guards inside. Resident (1 B). */
static int8_t  vault_room = -1;

static uint16_t level_seed(uint16_t d)
{
    return (uint16_t)(world_seed + (uint16_t)(d * 0x9E37u));
}

static void lvl_clear(void)
{
    uint8_t x, y;
    for (y = 0; y < MAPH; y++)
        for (x = 0; x < MAPW; x++)
            lvl[y][x] = ' ';
}

/* carve one cell of a corridor: door through a wall, '#' through rock */
static void carve(uint8_t x, uint8_t y)
{
    char c = lvl[y][x];
    if (c == '-' || c == '|') lvl[y][x] = '+';
    else if (c == ' ')        lvl[y][x] = '#';
}

static void dig_h(uint8_t x0, uint8_t x1, uint8_t y)
{
    if (x0 > x1) { uint8_t t = x0; x0 = x1; x1 = t; }
    for (; x0 <= x1; x0++) carve(x0, y);
}

static void dig_v(uint8_t y0, uint8_t y1, uint8_t x)
{
    if (y0 > y1) { uint8_t t = y0; y0 = y1; y1 = t; }
    for (; y0 <= y1; y0++) carve(x, y0);
}

static void dig_corridor(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    if (rn2(2)) { dig_h(x0, x1, y0); dig_v(y0, y1, x1); }
    else        { dig_v(y0, y1, x0); dig_h(x0, x1, y1); }
}

/* A corridor running along a wall turns the whole wall into a row of doors,
 * which looks odd. Thin those runs: a door flanked by doors/corridor on both
 * sides (same axis) becomes plain corridor, leaving real doors at the ends. */
static int doorish(char c) { return c == '+' || c == '#'; }

static void thin_doors(void)
{
    uint8_t x, y;
    for (y = 1; y < MAPH - 1; y++)
        for (x = 1; x < MAPW - 1; x++)
            if (lvl[y][x] == '+' &&
                ((doorish(lvl[y][x - 1]) && doorish(lvl[y][x + 1])) ||
                 (doorish(lvl[y - 1][x]) && doorish(lvl[y + 1][x]))))
                lvl[y][x] = '#';
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* Pick a door cell on room r's wall facing (tx,ty), plus the rock cell just
 * outside it, so corridors leave through a single door and run in the rock. */
static void room_door(uint8_t r, int tx, int ty,
                      uint8_t *doorx, uint8_t *doory, int *outx, int *outy)
{
    int cx = r_x[r] + r_w[r] / 2;
    int cy = r_y[r] + r_h[r] / 2;

    if (iabs(tx - cx) >= iabs(ty - cy)) {           /* left/right wall */
        uint8_t row = (uint8_t)(r_y[r] + 1 + rn2((uint8_t)(r_h[r] - 2)));
        if (tx >= cx) { *doorx = (uint8_t)(r_x[r] + r_w[r] - 1); *outx = (int)*doorx + 1; }
        else          { *doorx = r_x[r];                        *outx = (int)*doorx - 1; }
        *doory = row; *outy = row;
    } else {                                        /* top/bottom wall */
        uint8_t col = (uint8_t)(r_x[r] + 1 + rn2((uint8_t)(r_w[r] - 2)));
        if (ty >= cy) { *doory = (uint8_t)(r_y[r] + r_h[r] - 1); *outy = (int)*doory + 1; }
        else          { *doory = r_y[r];                        *outy = (int)*doory - 1; }
        *doorx = col; *outx = col;
    }
}

/* join two rooms: a door on each one's facing wall, corridor through the rock */
static void connect_rooms(uint8_t a, uint8_t b)
{
    uint8_t adx, ady, bdx, bdy;
    int aox, aoy, box, boy;
    int acx = r_x[a] + r_w[a] / 2, acy = r_y[a] + r_h[a] / 2;
    int bcx = r_x[b] + r_w[b] / 2, bcy = r_y[b] + r_h[b] / 2;

    room_door(a, bcx, bcy, &adx, &ady, &aox, &aoy);
    room_door(b, acx, acy, &bdx, &bdy, &box, &boy);
    lvl[ady][adx] = '+';
    lvl[bdy][bdx] = '+';
    dig_corridor((uint8_t)aox, (uint8_t)aoy, (uint8_t)box, (uint8_t)boy);
}

static void make_room(uint8_t i, uint8_t sx0, uint8_t sy0, uint8_t sx1, uint8_t sy1)
{
    uint8_t availw = (uint8_t)(sx1 - sx0 + 1);
    uint8_t availh = (uint8_t)(sy1 - sy0 + 1);
    uint8_t w = (uint8_t)(4 + rn2(8));   /* 4..11 */
    uint8_t h = (uint8_t)(3 + rn2(4));   /* 3..6  */
    uint8_t rx, ry, xx, yy;

    if (w > availw - 1) w = (uint8_t)(availw - 1);
    if (h > availh - 1) h = (uint8_t)(availh - 1);
    rx = (uint8_t)(sx0 + rn2((uint8_t)(availw - w)));
    ry = (uint8_t)(sy0 + rn2((uint8_t)(availh - h)));

    for (yy = ry; yy < ry + h; yy++) {
        for (xx = rx; xx < rx + w; xx++) {
            if (yy == ry || yy == ry + h - 1)      lvl[yy][xx] = '-';
            else if (xx == rx || xx == rx + w - 1) lvl[yy][xx] = '|';
            else                                   lvl[yy][xx] = '.';
        }
    }
    r_x[i] = rx; r_y[i] = ry; r_w[i] = w; r_h[i] = h;
}

void rand_floor(uint8_t i, uint8_t *px, uint8_t *py) __banked
{
    *px = (uint8_t)(r_x[i] + 1 + rn2((uint8_t)(r_w[i] - 2)));
    *py = (uint8_t)(r_y[i] + 1 + rn2((uint8_t)(r_h[i] - 2)));
}

/* A teleport destination: a random interior cell, but in a DIFFERENT room from
 * the one the hero stands in (when there's a choice), so reading a scroll of
 * teleportation actually relocates you instead of possibly dumping you back in
 * the same room -- or, worse, on your own square. A one-room level (e.g. the Big
 * Room) can only re-roll the cell. Runtime-only (reading a scroll), so the extra
 * rn2() calls don't touch the deterministic per-depth generation. */
void level_random_floor(uint8_t *px, uint8_t *py) __banked
{
    uint8_t cur = rcount, i, r;

    for (i = 0; i < rcount; i++)        /* which room (if any) is the hero in? */
        if ((uint8_t)hero_x >= r_x[i] && (uint8_t)hero_x < r_x[i] + r_w[i] &&
            (uint8_t)hero_y >= r_y[i] && (uint8_t)hero_y < r_y[i] + r_h[i]) {
            cur = i; break;
        }

    if (rcount > 1) {
        do { r = (uint8_t)rn2(rcount); } while (r == cur);   /* a different room */
        rand_floor(r, px, py);
    } else {                            /* only one room: just avoid your own cell */
        uint8_t tries = 0;
        do { rand_floor(0, px, py); }
        while (*px == (uint8_t)hero_x && *py == (uint8_t)hero_y && tries++ < 8);
    }
}

/* place a trackable floor item (so its pickup can be remembered) */
static void place_item(char ch)
{
    uint8_t i, x, y;
    if (icount >= 8) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;
    lvl[y][x] = ch;
    i_x[icount] = x; i_y[icount] = y; icount++;
}

static int item_at(uint8_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < icount; i++)
        if (i_x[i] == x && i_y[i] == y)
            return i;
    return -1;
}

static int gold_at(uint8_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < gcount; i++)
        if (g_x[i] == x && g_y[i] == y)
            return i;
    return -1;
}

static void place_gold(void)
{
    uint8_t i, x, y;
    if (gcount >= 8) return;
    i = rn2(rcount);
    rand_floor(i, &x, &y);
    if (lvl[y][x] != '.') return;
    lvl[y][x] = '$';
    g_x[gcount] = x; g_y[gcount] = y; gcount++;
}

/* Stock the shop room with priced items. Deterministic (no rn2, so it can't
 * desync the dungeon): items go on ~half the interior floor cells (the rest are
 * aisles), the class chosen per cell by a side hash, up to the 8-item cap. They
 * are tracked in i_*[] like normal floor items, so pickup/persistence just work;
 * resolve_floor() then prices each one (item.c). */
/* Reserve the shopkeeper's cell: an empty floor tile just inside a door if
 * there is one, else any interior floor. Chosen on the bare room (before
 * stocking) so it is always available. */
static void pick_keeper_cell(uint8_t r)
{
    uint8_t xx, yy, pass;
    keeper_x = (uint8_t)(r_x[r] + 1);              /* sane fallback */
    keeper_y = (uint8_t)(r_y[r] + 1);
    for (pass = 0; pass < 2; pass++)
        for (yy = (uint8_t)(r_y[r] + 1); yy + 1 < r_y[r] + r_h[r]; yy++)
            for (xx = (uint8_t)(r_x[r] + 1); xx + 1 < r_x[r] + r_w[r]; xx++) {
                if (lvl[yy][xx] != '.') continue;
                if (pass == 0 &&
                    !(lvl[yy][xx - 1] == '+' || lvl[yy][xx + 1] == '+' ||
                      lvl[yy - 1][xx] == '+' || lvl[yy + 1][xx] == '+'))
                    continue;
                keeper_x = xx; keeper_y = yy;
                return;
            }
}

static void stock_shop(uint8_t r)
{
    static const char SHOPCLS[6] = { ')', '[', '!', '?', '=', '%' };
    uint8_t xx, yy;
    for (yy = (uint8_t)(r_y[r] + 1); yy + 1 < r_y[r] + r_h[r] && icount < 8; yy++)
        for (xx = (uint8_t)(r_x[r] + 1); xx + 1 < r_x[r] + r_w[r] && icount < 8; xx++) {
            uint16_t h;
            if (lvl[yy][xx] != '.') continue;          /* skip stairs etc.   */
            if (xx == keeper_x && yy == keeper_y) continue;  /* keep keeper cell clear */
            h = (uint16_t)(world_seed + (uint16_t)dlvl * 2657u
                           + (uint16_t)xx * 131u + (uint16_t)yy * 1009u);
            if (h & 1) continue;                       /* leave ~half as aisle */
            lvl[yy][xx] = SHOPCLS[h % 6u];
            i_x[icount] = xx; i_y[icount] = yy; icount++;
        }
}

static int cell_in_room(uint8_t r, uint8_t x, uint8_t y)
{
    return x >= r_x[r] && x < r_x[r] + r_w[r] &&
           y >= r_y[r] && y < r_y[r] + r_h[r];
}

/* Pack a treasure vault: gold piles + valuable items over the room's interior,
 * up to the 8+8 persistence caps (the rest stays floor, so there is room to move
 * and fight the guards). Deterministic (a side hash per cell, no rn2), exactly
 * like stock_shop, so the vault can't desync the per-depth generation and its
 * loot is tracked in the gold/item arrays for normal pickup and persistence. */
static void fill_vault(uint8_t r)
{
    static const char VCLS[5] = { ')', '[', '!', '?', '=' };
    uint8_t xx, yy;
    for (yy = (uint8_t)(r_y[r] + 1); yy + 1 < r_y[r] + r_h[r]; yy++)
        for (xx = (uint8_t)(r_x[r] + 1); xx + 1 < r_x[r] + r_w[r]; xx++) {
            uint16_t h;
            if (lvl[yy][xx] != '.') continue;          /* skip any stair etc. */
            h = (uint16_t)(world_seed + (uint16_t)dlvl * 3911u
                           + (uint16_t)xx * 131u + (uint16_t)yy * 1009u);
            if ((h % 3u) == 0) {
                if (gcount >= 8) continue;
                lvl[yy][xx] = '$';
                g_x[gcount] = xx; g_y[gcount] = yy; gcount++;
            } else {
                if (icount >= 8) continue;
                lvl[yy][xx] = VCLS[(uint8_t)((h >> 2) % 5u)];
                i_x[icount] = xx; i_y[icount] = yy; icount++;
            }
        }
}

/* ---- special levels (Phase 22+) -------------------------------------------
 * Some depths are landmark "special" levels with a hand-built layout instead of
 * the procedural rooms+corridors. ONE deterministic dispatcher (special_gen)
 * sits at the top of gen_level: the per-depth decision (special_kind) is a fixed
 * predicate that NEVER calls rn2, so non-special depths generate byte-for-byte
 * as before and their deterministic persistence stays in sync. A chosen
 * generator builds terrain + sets rcount/r_*[] (so FOV lights its rooms) +
 * up_x/dn_x, after which gen_level returns early. Big Room is the first kind;
 * the treasure vault and hand-drawn templates (Phases 23-24) plug in here. */
enum { SP_NONE = 0, SP_BIGROOM, SP_TEMPLATE };

/* Which special level (if any) lives at depth d. Fixed predicate, no rn2. The
 * win level (DLVL_AMULET) is always left normal. Big Room recurs every 11th
 * depth (11, 22, 33, 44); a hand-drawn template appears on ~1/9 of the other
 * depths (>= 3), chosen by a side hash so non-special depths are unchanged. */
static uint8_t special_kind(uint16_t d)
{
    uint16_t th;
    if (d == DLVL_AMULET) return SP_NONE;
    if (d >= 2 && (d % 11u) == 0) return SP_BIGROOM;
    th = (uint16_t)(world_seed * 3331u + (uint16_t)d * 5779u + 23u);
    th ^= (uint16_t)(th << 7);
    th ^= (uint16_t)(th >> 9);
    if (d >= 3 && (th % 9u) == 0) return SP_TEMPLATE;
    return SP_NONE;
}

/* Big Room: one giant lit chamber filling the whole playable area. rcount=1
 * with the room in r_*[0], so FOV lights the entire room the moment you arrive
 * (you step in on a stair placed inside it). The two stairs go on random
 * interior cells. rn2 here is fine: this depth reseeded, so the stream is
 * deterministic per depth and isolated from every other level. */
static void gen_big_room(void)
{
    uint8_t rx = PX0, ry = PY0;
    uint8_t rw = (uint8_t)(PX1 - PX0 + 1);
    uint8_t rh = (uint8_t)(PY1 - PY0 + 1);
    uint8_t x, y, ux, uy, dx, dy;

    for (y = ry; y < ry + rh; y++)
        for (x = rx; x < rx + rw; x++) {
            if (y == ry || y == (uint8_t)(ry + rh - 1))      lvl[y][x] = '-';
            else if (x == rx || x == (uint8_t)(rx + rw - 1)) lvl[y][x] = '|';
            else                                             lvl[y][x] = '.';
        }

    rcount = 1;
    r_x[0] = rx; r_y[0] = ry; r_w[0] = rw; r_h[0] = rh;

    rand_floor(0, &ux, &uy);
    do { rand_floor(0, &dx, &dy); } while (dx == ux && dy == uy);
    lvl[uy][ux] = '<'; up_x = ux; up_y = uy;
    lvl[dy][dx] = '>'; dn_x = dx; dn_y = dy;
}

/* Dispatcher: if this depth is special, fully build its level (terrain, rooms,
 * stairs, loot) and return 1 so gen_level returns early; else return 0 for the
 * normal procedural path. Special levels get scattered loot but no shop (a shop
 * over the whole big room makes no sense). Monsters are spawned later in
 * build_level and land in the big room automatically (rcount=1). */
static int special_gen(void)
{
    uint8_t k = special_kind(dlvl);

    if (k == SP_BIGROOM) {
        gen_big_room();
    } else if (k == SP_TEMPLATE) {
        /* stamp a hand-drawn map: terrain + stairs + the chambers' r_*[] rects */
        load_template((uint8_t)(level_seed(dlvl) % template_count()));
    } else {
        return 0;          /* normal procedural level */
    }

    /* both special kinds set rcount + r_*[], so scatter loot through the rooms
     * (no shop); monsters spawn there too via build_level -> spawn_level_monsters */
    place_gold();
    place_gold();
    place_gold();
    place_item('%');      /* food   */
    place_item(')');      /* weapon */
    place_item('[');      /* armor  */
    place_item('!');      /* potion */
    place_item('!');
    if (rn2(2))    place_item('?');     /* scroll */
    if (dlvl >= 3) place_item('=');     /* ring   */
    return 1;
}

void gen_level(void) __banked
{
    uint8_t i, j;
    uint8_t secw = (uint8_t)((PX1 - PX0 + 1) / SECT_COLS);
    uint8_t sech = (uint8_t)((PY1 - PY0 + 1) / SECT_ROWS);

    rng_set(level_seed(dlvl));     /* deterministic layout for this depth */

    lvl_clear();
    rcount = 0;
    gcount = 0;
    icount = 0;
    shop_room  = -1;   /* reset for EVERY level: special levels (Big Room/template) */
    vault_room = -1;   /* return early below, so they must not inherit the last     */
                       /* level's shop/vault (else phantom billing + a stray keeper) */

    if (special_gen())
        return;        /* special level fully built (terrain, rooms, stairs, loot) */

    for (j = 0; j < SECT_ROWS; j++) {
        for (i = 0; i < SECT_COLS; i++) {
            uint8_t sx0 = (uint8_t)(PX0 + i * secw);
            uint8_t sy0 = (uint8_t)(PY0 + j * sech);
            uint8_t sx1 = (i == SECT_COLS - 1) ? PX1 : (uint8_t)(sx0 + secw - 1);
            uint8_t sy1 = (j == SECT_ROWS - 1) ? PY1 : (uint8_t)(sy0 + sech - 1);
            make_room(rcount, sx0, sy0, sx1, sy1);
            rcount++;
        }
    }

    /* Connect grid-adjacent rooms so every door leads to a real neighbour and
     * corridors stay short (no long diagonal runs across the whole map):
     * chain each row horizontally, then link the rows with one vertical hop. */
    for (j = 0; j < SECT_ROWS; j++)
        for (i = 0; i + 1 < SECT_COLS; i++)
            connect_rooms((uint8_t)(j * SECT_COLS + i),
                          (uint8_t)(j * SECT_COLS + i + 1));
    for (j = 0; j + 1 < SECT_ROWS; j++)
        connect_rooms((uint8_t)(j * SECT_COLS),
                      (uint8_t)((j + 1) * SECT_COLS));
    thin_doors();

    /* stairs in two different random rooms (so their location varies) */
    {
        uint8_t x, y, upr, dnr;
        upr = rn2(rcount);
        do { dnr = rn2(rcount); } while (dnr == upr);
        rand_floor(upr, &x, &y);
        lvl[y][x] = '<';
        up_x = x; up_y = y;
        rand_floor(dnr, &x, &y);
        lvl[y][x] = '>';
        dn_x = x; dn_y = y;
    }

    /* loot: a sealed treasure vault on a few deep levels (takes precedence),
     * else a shop on ~1/3 of levels (depth >= 2), else scattered items. Both the
     * vault and shop decisions use side hashes (no rn2), so non-special levels
     * generate exactly as before and the deterministic persistence stays in sync.
     * The vault is a leaf room (one door, never a through-route) that holds no
     * stairs, so sealing it as treasure never cuts the level in two.
     * (shop_room/vault_room were already reset at the top of gen_level.) */
    {
        uint16_t vh = (uint16_t)(world_seed * 2179u + (uint16_t)dlvl * 6863u + 7u);
        vh ^= (uint16_t)(vh << 7);
        vh ^= (uint16_t)(vh >> 9);
        if (dlvl >= 4 && dlvl != DLVL_AMULET && (vh % 7u) == 0) {
            uint8_t k;
            for (k = 0; k < SECT_ROWS; k++) {
                uint8_t r = (uint8_t)((((uint8_t)(vh >> 4) + k) % SECT_ROWS)
                                      * SECT_COLS + (SECT_COLS - 1));   /* a leaf room */
                if (cell_in_room(r, up_x, up_y) || cell_in_room(r, dn_x, dn_y))
                    continue;                              /* keep the stairs reachable */
                if (r_w[r] < 5 || r_h[r] < 4) continue;    /* a chamber, not a 1-row strip */
                vault_room = (int8_t)r;
                break;
            }
            if (vault_room >= 0)
                fill_vault((uint8_t)vault_room);
        }
    }
    if (vault_room < 0) {
        uint16_t sh = (uint16_t)(world_seed * 1009u + (uint16_t)dlvl * 2657u + 13u);
        sh ^= (uint16_t)(sh << 7);
        sh ^= (uint16_t)(sh >> 9);
        if (dlvl >= 2 && dlvl != DLVL_AMULET && (sh % 3u) == 0) {
            uint8_t tries = 0;
            shop_room = (int8_t)((sh >> 5) % rcount);
            /* the shop must miss the stairs (a staircase in a shop is odd) and be
             * big enough to stock a few items (>= 6x4): otherwise skip to the next
             * room, and if none qualifies, fall through to scattered loot. */
            while (tries < rcount &&
                   (cell_in_room((uint8_t)shop_room, up_x, up_y) ||
                    cell_in_room((uint8_t)shop_room, dn_x, dn_y) ||
                    r_w[shop_room] < 6 || r_h[shop_room] < 4)) {
                shop_room = (int8_t)(((uint8_t)shop_room + 1u) % rcount);
                tries++;
            }
            if (tries >= rcount) shop_room = -1;   /* no roomy stair-free room */
        }
        if (shop_room >= 0) {
            pick_keeper_cell((uint8_t)shop_room);
            stock_shop((uint8_t)shop_room);
        } else {
            place_gold();
            place_gold();
            place_item('%');      /* food   */
            place_item(')');      /* weapon */
            place_item('[');      /* armor  */
            place_item('!');      /* potion */
            if (dlvl >= 2) place_item('!');
            if (rn2(2))    place_item('?');     /* scroll */
            if (dlvl >= 3 && rn2(2)) place_item('=');   /* ring */
        }
    }

    /* The deepest level holds the Amulet of Yendor instead of a way down:
     * put it on the would-be down-stairs cell (a guaranteed room-floor tile).
     * This needs no RNG, so it can't shift the deterministic gold/item indices
     * or change this level's monster spawns. */
    if (dlvl == DLVL_AMULET)
        lvl[dn_y][dn_x] = has_amulet ? '.' : '"';
}

/* is (x,y) inside the current level's shop room? (item.c uses this to bill) */
int shop_in_room(int x, int y) __banked
{
    uint8_t r;
    if (shop_room < 0) return 0;
    r = (uint8_t)shop_room;
    return x >= r_x[r] && x < r_x[r] + r_w[r] &&
           y >= r_y[r] && y < r_y[r] + r_h[r];
}

/* the shop room's rectangle, so the renderer can recolour its walls; 0 = no shop */
int shop_rect(uint8_t *sx, uint8_t *sy, uint8_t *sw, uint8_t *sh) __banked
{
    uint8_t r;
    if (shop_room < 0) return 0;
    r = (uint8_t)shop_room;
    *sx = r_x[r]; *sy = r_y[r]; *sw = r_w[r]; *sh = r_h[r];
    return 1;
}

/* the shopkeeper's reserved cell (chosen in gen_level). 0 if no shop. */
int shop_keeper_xy(uint8_t *kx, uint8_t *ky) __banked
{
    if (shop_room < 0) return 0;
    *kx = keeper_x; *ky = keeper_y;
    return 1;
}

/* index of this level's treasure-vault room, or -1 (monster_ai posts guards) */
int level_vault_room(void) __banked
{
    return vault_room;
}

/* is (x,y) inside the treasure vault? (item.c upgrades vault loot quality) */
int in_vault_room(int x, int y) __banked
{
    uint8_t r;
    if (vault_room < 0) return 0;
    r = (uint8_t)vault_room;
    return x >= r_x[r] && x < r_x[r] + r_w[r] &&
           y >= r_y[r] && y < r_y[r] + r_h[r];
}

void apply_gold_persistence(void) __banked
{
    uint8_t b;
    if (dlvl > MAXLVL) return;
    for (b = 0; b < gcount; b++)
        if (gold_taken[dlvl] & (uint8_t)(1u << b))
            lvl[g_y[b]][g_x[b]] = '.';
}

int level_take_gold(uint8_t x, uint8_t y) __banked
{
    int gi = gold_at(x, y);
    lvl[y][x] = '.';
    if (gi >= 0 && dlvl <= MAXLVL)
        gold_taken[dlvl] |= (uint8_t)(1u << gi);
    return gi >= 0;
}

void apply_item_persistence(void) __banked
{
    uint8_t b;
    if (dlvl > MAXLVL) return;
    for (b = 0; b < icount; b++)
        if (item_taken[dlvl] & (uint8_t)(1u << b))
            lvl[i_y[b]][i_x[b]] = '.';
}

int level_take_item(uint8_t x, uint8_t y) __banked
{
    int ii = item_at(x, y);
    lvl[y][x] = '.';
    if (ii >= 0 && dlvl <= MAXLVL)
        item_taken[dlvl] |= (uint8_t)(1u << ii);
    return ii >= 0;
}

void level_reset_persistence(void) __banked
{
    uint8_t i;
    for (i = 0; i <= MAXLVL; i++) {
        gold_taken[i] = 0;
        item_taken[i] = 0;
    }
}
