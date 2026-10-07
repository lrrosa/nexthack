/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* monster_ai.c - the HOT banked third of the monster module: the per-turn BFS
 * chase, combat and experience. Split out of monster.c so this cold-ish code
 * lives in a banked page (mapped into the 0xC000 window on demand); the
 * once-per-level spawning and the mon_dead save/restore were split out again
 * to monster_spawn.c (v0.11) when this bank filled to the brim.
 *
 * The monster arrays and the per-cell lookups (monster_at/mon_find/pick_mon)
 * stay RESIDENT in monster.c; this file reaches them by direct (resident)
 * calls. Its own entry points are __banked (see monster.h). */

#include "monster.h"
#include "level.h"        /* lvl, terrain, rand_floor, rcount, up_x/up_y      */
#include "platform.h"     /* msg/msg2, file_read/file_write                   */
#include "rng.h"          /* rn2                                              */
#include "game.h"         /* hero/php/dead/dlvl, xp/xlvl, weapon_dmg/armor_def */
#include "sfx.h"          /* sound effects                                    */
#include "item.h"         /* corrode_worn                                     */

/* Const-banked (banks.json): this file's message literals live in its own
 * bank (consumed by msg/msg2 while it is mapped), not the tight resident
 * half. Don't pass them into another bank's __banked functions. */

/* Spawning and the mon_dead persistence/save live in monster_spawn.c; the
 * combat below sets mon_dead's kill bits directly (defined in monster.c).
 * place_pet lives in nexthack.c's bank to keep this one under its 16 KB. */

static int iabs(int v) { return v < 0 ? -v : v; }

/* ---- the wielded artifact (art_fx, recomputed in item.c) ---- */

/* does the weapon in your hand hunt this kind -- an artifact's quarry, or what
 * silver burns? Then the blow lands double, NetHack's slaying bonus at its
 * simplest */
static uint8_t art_slays(char ch)
{
    if (!art_fx) return 0;
    return (uint8_t)(((art_fx & AF_ORCS)    && ch == 'o') ||
                     ((art_fx & AF_UNDEAD)  && (ch == 'Z' || ch == 'W' || ch == 'V')) ||
                     ((art_fx & AF_TROLLS)  && ch == 'T') ||
                     ((art_fx & AF_DRAGONS) && ch == 'D') ||
                     ((art_fx & AF_SILVER)  && (ch == 'V' || ch == 'i')));
}

/* Sting glows blue while an orc is within eight squares, seen or not, as
 * NetHack's elven blades warn of orcs -- one message as it lights and one as
 * it goes out, so it says something only when the news changes. */
static uint8_t sting_lit;

/* No monster strikes you this turn: you stand on a live Elbereth or on a
 * scroll of scare monster (item.c). Worked out once per monsters_turn --
 * the scroll means a look at the floor under you -- for both chase paths. */
static uint8_t ward;
static void sting_glow(void)
{
    uint8_t i, near = 0;
    if (art_fx & AF_ORCS)
        for (i = 0; i < mcount; i++)
            if (m_alive[i] && m_type[i] == 'o' &&
                iabs((int)m_x[i] - hero_x) <= 8 && iabs((int)m_y[i] - hero_y) <= 8) {
                near = 1;
                break;
            }
    if (near == sting_lit) return;
    sting_lit = near;
    if (art_fx & AF_ORCS) msg(near ? "Sting glows blue!" : "Sting stops glowing.");
}

/* ---- experience ---- */
static void gain_xp(uint8_t amt)
{
    xp = (uint16_t)(xp + amt);
    while (xlvl < 30 && xp >= (uint16_t)xlvl * 20) {
        uint8_t gain = (uint8_t)(rn2(4) + 2 +
                                 (at_con >= 14 ? 1 : 0)); /* 2..5 max HP,
                                                           * +1 if hardy */
        uint8_t pwg = (uint8_t)(at_int >= 14 ? 2 : 1);    /* the mind grows too */
        xlvl++;
        /* Cap like the other two max-HP writers (the altar boon and the
         * gain-level potion both guard with `< 250`). Without it a hero at
         * 252 -- legitimately reachable, since the altar's guard lets it add
         * 3 on top of 249 -- wrapped the uint8_t and came out of the level-up
         * with 2 max HP instead of 255. */
        if (pmaxhp + gain > 250)               /* both promote to int here */
            gain = (pmaxhp >= 250) ? 0 : (uint8_t)(250 - pmaxhp);
        pmaxhp = (uint8_t)(pmaxhp + gain);
        php = (uint8_t)(php + gain);
        if (pmaxpw < 30) {
            pmaxpw = (uint8_t)(pmaxpw + pwg);
            pw = (uint8_t)(pw + pwg);
        }
        msg("Welcome to a new level!");
        sfx_levelup();
    }
}

/* One whole experience level, bottled: the potion of gain level (item.c).
 * Jumps xp to the current threshold and lets gain_xp's shared loop do the
 * bump, so the level-up rules live in exactly one place. */
void level_up(void) __banked
{
    if (xlvl >= 30) return;
    xp = (uint16_t)((uint16_t)xlvl * 20);
    gain_xp(0);
}

/* Apply dmg to monster mi: kill it (with XP) or wound it, with the matching
 * "You kill/hit the X" message and sound. Shared by melee (attack_monster) and
 * thrown weapons (item.c do_throw). Does not consume a turn -- the caller does. */
void hit_monster(uint8_t mi, uint8_t dmg) __banked
{
    const MonType *mt = mon_find(m_type[mi]);
    uint8_t was_peace = m_peace[mi];
    if (m_type[mi] == 'x') m_type[mi] = 'm';   /* struck bait drops the disguise */
    m_sleep[mi] = 0;                           /* any hit wakes it            */
    if (was_peace) {
        /* First blood on a townsman angers the whole town -- every peaceful
         * on the level turns hostile -- and the gods frown on it (luck). */
        uint8_t j;
        for (j = 0; j < mcount; j++) m_peace[j] = 0;
        luck = (int8_t)(luck - 2);
        if (luck < -5) luck = -5;
    }
    if (m_hp[mi] <= dmg) {
        m_alive[mi] = 0;
        m_blind[mi] = 0;
        if (dlvl <= MAXLVL)     /* remember the kill -- of the level's own only */
            mon_dead[dlvl] |= (uint8_t)(m_track & (1u << mi));
        drop_held(mi);                               /* stolen goods return */
        if (m_type[mi] != MON_KEEPER) {
            if (rn2(4) == 0)                         /* it may leave loot... */
                death_drop(m_x[mi], m_y[mi]);
            else if (rn2(2))                         /* ...or its corpse     */
                corpse_drop(m_x[mi], m_y[mi], m_type[mi]);
            if (m_type[mi] == 'h' && rn2(3) == 0)    /* a dwarf's pick-axe   */
                dwarf_pick(m_x[mi], m_y[mi]);
        }
        {   /* the killing blow gets a little colour (runtime rn2: gen-safe) */
            static const char *const killv[3] =
                { "You kill the ", "You slay the ", "You destroy the " };
            msg2(was_peace ? "You murder the " : killv[rn2(3)], mt->name, "!");
        }
        sfx_kill();
        cnt_kills++;                    /* by your hand (conducts) */
        gain_xp(mt->xp);
    } else {
        static const char *const hitv[3] =
            { "You hit the ", "You smite the ", "You whack the " };
        m_hp[mi] = (uint8_t)(m_hp[mi] - dmg);
        msg2(hitv[rn2(3)], mt->name, ".");
        sfx_hit();
    }
}

void attack_monster(uint8_t mi) __banked
{
    const MonType *mt = mon_find(m_type[mi]);
    uint8_t dmg;

    turns++;
    /* Dexterity decides whether the swing lands at all (Dx 11 = 85%, 16+ =
     * always) -- the whiffed turn still passes, as in NetHack. Luck leans on
     * the die: pleased gods steady your hand, spurned ones shake it. A
     * SLEEPING target can't dodge: the sneak attack always lands. */
    if (!m_sleep[mi] && !m_blind[mi] &&
        rn2(20) >= (uint8_t)(12 + (at_dex >> 1) + (eff_luck() >> 1))) {
        msg2("You miss the ", mon_name(m_type[mi]), ".");
        return;
    }
    dmg = (uint8_t)(rn2(4) + 1 + weapon_dmg);   /* 1..4 + weapon */
    if (at_str >= 17)      dmg = (uint8_t)(dmg + 2);   /* strength bonus */
    else if (at_str >= 14) dmg++;
    /* The Rogue's backstab (NetHack's, on the fleeing; here on the sleeping,
     * which stealth lets her reach): d(level) + level/4 on top. Measured to
     * put balance.py's Rogue with the dog at ~35% of runs won. */
    if (pclass == PC_ROGUE && m_sleep[mi])
        dmg = (uint8_t)(dmg + rn2(xlvl) + 1 + (xlvl >> 2));
    if (art_slays(mt->ch)) dmg = (uint8_t)(dmg << 1);  /* the artifact's quarry */
    hit_monster(mi, dmg);
    if (art_fx & AF_DRAIN) {        /* Stormbringer drinks: half the blow is yours */
        ADD_SAT8(php, dmg >> 1);
        if (php > pmaxhp) php = pmaxhp;
    }
    if (mt->corr && rn2(2))         /* acid eats the weapon you strike it with */
        corrode_worn(')');
    if (mt->ch == 'e' && m_alive[mi] && !st_blind) {
        /* you met the floating eye's gaze mid-swing -- the classic freeze.
         * A blind hero can't meet it (and telepathy makes blind-fighting
         * eyes the NetHack-approved trick). A dead eye has no gaze: the blow
         * that killed it used to freeze you all the same, and bury the kill
         * message under "You are frozen" -- NetHack's passive needs it alive. */
        if (ring_fx & RF_FREEACT) {     /* free action holds: NetHack's line */
            ring_noticed(RF_FREEACT);
            msg("You momentarily stiffen.");
        } else {
            ADD_SAT8(st_sleep, rn2(5) + 2);
            msg("You are frozen by its gaze!");
        }
    }
}

/* ---- monster turn: chase the hero, attack when adjacent ---- */

static void monster_hits_player(uint8_t i)
{
    const MonType *mt = mon_find(m_type[i]);
    uint8_t bite = (uint8_t)(rn2(mt->dmg) + 1 + eff_depth() / 4);  /* harder deep */
    uint8_t grazed = 0;      /* the armour covered it: 1 damage, its own line */

    /* Armour SUBTRACTS; it no longer cancels. The old rule made a covered
     * blow a clean miss, which was all-or-nothing at both ends of the
     * dungeon: it left the first twenty floors harmless (86% of blows simply
     * bounced on Dlvl 1) and had nothing left to give on the last twenty,
     * where no reachable armour covers a bite of 13..20. A covered blow now
     * grazes for 1 instead -- armour still feels like armour early without
     * making the hero untouchable. Measured with tools/balance.py: a fight
     * costs 0.1% -> 0.4% of the hero's bar on Dlvl 3 and 0.4% -> 2.1% on
     * Dlvl 10, while every depth past 20 is unchanged. */
    if (armor_def >= bite) {
        bite = 1;                            /* turned aside, not for free */
        grazed = 1;
    } else {
        bite = (uint8_t)(bite - armor_def);
    }
    sfx_hurt();

    if (php <= bite) {
        php = 0; dead = 1;
        msg2("The ", mt->name, " kills you!");
    } else {
        static const char *const bitev[3] =
            { " bites you!", " hits you!", " tears at you!" };
        php = (uint8_t)(php - bite);
        if (grazed) msg2("You shrug off the ", mt->name, "!");
        else        msg2("The ", mt->name, bitev[rn2(3)]);
        if (mt->corr && rn2(2))     /* acid/rust corrodes your worn armour */
            corrode_worn('[');
        switch (mt->atk) {          /* special on-hit effects (status-effect layer) */
        case ATK_POISON:
            if (poison_res()) break;   /* immune flesh, or the amulet */
            if (rn2(2)) { ADD_SAT8(st_poison, rn2(4) + 3);
                          msg("You feel poisoned!"); }
            break;
        case ATK_BLIND:
            if (rn2(2)) { ADD_SAT8(st_blind, rn2(15) + 10);
                          map_dirty = 1; msg("You are blinded!"); }
            break;
        case ATK_STEAL:
            if (gold > 0) {
                gold = (uint16_t)(gold >> 1);   /* grabs about half of it... */
                m_alive[i] = 0;                 /* ...then vanishes with the loot */
                msg2("The ", mt->name, " steals your gold!");
            }
            break;
        case ATK_SLEEP:
            if (intrinsics & INTR_SLEEP_RES) break;    /* wide awake */
            if (rn2(3) == 0) { ADD_SAT8(st_sleep, rn2(4) + 3);
                               msg("You are put to sleep!"); }
            break;
        case ATK_DRAIN:
            if (art_fx & AF_DRAINRES) break;    /* Excalibur, Stormbringer: as in NetHack */
            if (rn2(2) && pmaxhp > 2) {         /* a wraith saps your life force */
                pmaxhp--;
                if (php > pmaxhp) php = pmaxhp;
                msg("You feel drained!");
            }
            break;
        case ATK_ITEM:
            steal_item(i);   /* the nymph lifts an item and blinks away (item.c) */
            break;
        }
    }
}

/* A spawn sleeper (m_sleep 255, set by monster_spawn.c's side hash) sleeps
 * until disturbed -- 255 is a sentinel, not a timer. It stirs when the hero
 * is close: adjacent always wakes it, within 5 it wakes 1-in-3 a turn (your
 * footsteps); any hit wakes it at once (hit_monster/pet_hits). On waking it
 * acts the same turn. Returns 1 while it stays asleep. */
static uint8_t still_asleep(uint8_t i)
{
    int dx, dy;
    if (m_sleep[i] != 255) return 0;
    /* A ring of aggravate monster: nothing sleeps through you. A ring of
     * stealth: your footsteps wake nobody, and even a sleeper you stand
     * beside only stirs one turn in three -- time for the sneak attack.
     * Without either ring the rn2 calls are exactly the old ones. */
    if (ring_fx & RF_AGGR) { m_sleep[i] = 0; return 0; }
    dx = iabs((int)m_x[i] - hero_x);
    dy = iabs((int)m_y[i] - hero_y);
    if ((dx <= 1 && dy <= 1) ? (!stealthy() || rn2(3) == 0)
                             : (dx <= 5 && dy <= 5 &&
                                !stealthy() && rn2(3) == 0)) {
        m_sleep[i] = 0;              /* it stirs awake */
        return 0;
    }
    return 1;
}

/* A peaceful (Minetown townsfolk) never chases nor strikes: it just mills
 * about its business -- an occasional step to a random free neighbour. */
static void peace_amble(uint8_t i)
{
    int nx, ny;
    char t;
    if (rn2(3)) return;                    /* mostly it stands and haggles */
    nx = (int)m_x[i] + (int)rn2(3) - 1;
    ny = (int)m_y[i] + (int)rn2(3) - 1;
    if (nx == (int)m_x[i] && ny == (int)m_y[i]) return;
    if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) return;
    t = lvl[ny][nx];
    if (t == '|' || t == '-' || t == ' ') return;
    if (nx == hero_x && ny == hero_y) return;
    if (monster_at(nx, ny) >= 0) return;
    if (shop_in_room(nx, ny)) return;      /* the shop is the keeper's floor */
    mon_face_to(i, (uint8_t)nx);
    m_x[i] = (uint8_t)nx; m_y[i] = (uint8_t)ny;
}

/* ---- pathfinding: a BFS distance field from the hero ("Dijkstra map").
 * Computed once per turn; each monster then steps to the neighbouring cell
 * with the smallest distance, which routes optimally around walls.        */

#define UNREACH 255
#ifndef __ZXNEXT
/* +zx (3.5 MHz) only: cap the BFS this far from the hero, and skip it unless a
 * live ENEMY is within MON_WAKE. The dog heels greedily without the flood, and
 * wakes it for itself only when a wall boxes the greedy step (see monsters_turn).
 * The Next (28 MHz, whole 80-wide map visible) keeps the unbounded chase. */
#define MAXDIST  30
#define MON_WAKE 22
#endif
/* BFS frontier queue: a RING. A flood only ever holds what is left of one
 * distance layer plus the start of the next, and on these maps that is small
 * -- 67 cells at most, measured over the Big Room and all six templates --
 * while the flood itself reaches up to 1292 cells. The old queue was linear
 * and counted every cell EVER enqueued against its 696 entries, so on the
 * Big Room, the cavern and the maze it simply stopped: 101 to 578 walkable
 * cells kept UNREACH, and a monster standing on one had no way to you.
 * 256 entries so the uint8_t head/tail wrap by themselves -- it MUST stay
 * 256 -- and the enqueue is still guarded, so a map drawn to defeat it
 * leaves cells unexpanded instead of overflowing. In Bank 5 behind dist[]:
 * 0x7A90-0x7C90, which frees the 880 B above it up to 0x8000. */
#define BFSQ_SIZE 256

/* dist[] lives in Bank 5's free space (after the 80x32x2 tilemap, 0x7400),
 * which the CPU always sees at 0x4000-0x7FFF (segment 1) and which code banking
 * (segment 3, 0xC000) never touches. Placing this 1680-byte per-turn scratch
 * map there frees that much of the tight resident BSS budget. Safe because it
 * is rewritten every turn and never read during esxDOS file I/O. NOTE: 0x7400
 * assumes the tilemap ends there (TILEMAP_BASE 0x6000 + 80*32*2) -- keep in
 * sync with platform.c if the tilemap geometry changes.
 * SDCC rejects casts to a pointer-to-array type, so index the flat view as
 * dist[y*MAPW + x] (compute_dist_map works through the flat `d`). */
#define dist ((uint8_t *)0x7400u)
/* bfsq also lives in Bank 5 (right after dist), same rationale: pure per-turn
 * BFS scratch, never touched during esxDOS file I/O. */
#define bfsq ((uint16_t *)0x7A90u)

#ifndef __ZXNEXT
/* Hand-written Z80 fill of the 1680-byte dist[] (puttile_asm.asm). Clearing the
 * whole map to UNREACH every turn is the BFS's biggest cost on the 3.5 MHz 128K
 * -- profiled at 1680 cell-writes/turn vs the flood's ~112 -- and a tight
 * unrolled fill beats SDCC's scalar loop ~3x. The Next (28 MHz) keeps the C
 * loop. Keep the 1680 in dist_clear in sync with MAPH*MAPW. */
extern void dist_clear(uint8_t *p);
#endif

#define TARGET 254      /* a cell whose monster reads the field this turn */

/* One neighbour of the cell being expanded: `off` (0..2) is its step from
 * the row base dq/lq, `poff` the same step in the packed (y << 8) | x queue
 * word. Unrolled -- the eight neighbours as straight-line code -- because the
 * dx/dy loops kept their ints in memory, most of each neighbour's cost being
 * IX-indexed loop bookkeeping around a one-byte test. The test order is the
 * loops' old order.
 *
 * Every pointer offset here is 0, 1 or 2, never negative, and that is load-
 * bearing: for `lc[-81]` SDCC emitted the high byte as `+((0xffffffaf)/256)`,
 * which z80asm divides SIGNED -- -81/256 = 0 -- so the wall test read a byte
 * two rows BELOW and the flood leaked through rock (caught in ZEsarUX on the
 * maze). Row bases one cell left of the column, from integer arithmetic,
 * keep the constants non-negative. */
#define ENQ_OK(nd) ((uint16_t)(tail - head) < BFSQ_SIZE - 1)
#define FLOOD_TRY(off, poff)                                                  \
    do {                                                                      \
        uint8_t *q = dq + (off);                                              \
        if (*q >= TARGET) {                          /* not labelled yet */   \
            char c = lq[off];                        /* walkable? */          \
            if (c != '|' && c != '-' && c != ' ') {                           \
                if (*q == TARGET && --want == 0) { *q = nd; return; }         \
                *q = nd;                                                      \
                if (ENQ_OK(nd)) bfsq[(uint8_t)tail++] = (uint16_t)(p + (poff)); \
            }                                                                 \
        }                                                                     \
    } while (0)

#ifndef __ZXNEXT
/* +zx: the flood's main loop in hand-written Z80. The C loop below (still
 * the Next's) keeps its locals in IX-indexed memory, and a boxed-in monster
 * makes it run every turn: measured in MAME, a flood out to MAXDIST cost
 * ~100 ms of a 160 ms turn. This is that loop, step for step -- the same
 * neighbour order and tests (FLOOD_TRY), the MAXDIST horizon and the ring
 * guard -- so the field it leaves is the C one's, cell for cell. It lives
 * here rather than in a .asm so that it is always in monster_ai's own bank,
 * whichever banks.json gives it, and compute_dist_map reaches it directly.
 *
 * compute_dist_map sets up the field, the queue and fl_want, then calls it.
 * IX -> dist[] of the cell being expanded, HL -> its lvl[] char, B = its
 * label + 1, C = which neighbours exist (bit 0 left, 1 right, 2 up, 3 down).
 * fl_x/fl_y hold the cell; a queue entry is its (x, y) byte pair, as in C. */
static uint8_t fl_head, fl_tail, fl_want, fl_x, fl_y;

static void flood_run(void) __naked
{
    __asm
FL_DIST     equ 0x7400              ; dist[], as #defined above
FL_BFSQ     equ 0x7a90              ; bfsq[]
FL_MAXDIST  equ 30                  ; MAXDIST
FL_TARGET   equ 254                 ; TARGET
FL_MAPW     equ 80
FL_MAPH     equ 21

; One neighbour at dist offset OFF, map step (DX, DY): FLOOD_TRY.
FL_TRY MACRO OFF, DX, DY, SKIP, KEEP
    ld   a, (ix+OFF)
    cp   FL_TARGET
    jr   c, SKIP                    ; labelled already
    push hl
    ld   de, OFF
    add  hl, de
    ld   a, (hl)                    ; its map char: walkable?
    pop  hl
    cp   0x7c                       ; |
    jr   z, SKIP
    cp   0x2d                       ; -
    jr   z, SKIP
    cp   0x20                       ; rock
    jr   z, SKIP
    ld   a, (ix+OFF)
    ld   (ix+OFF), b                ; label it
    cp   FL_TARGET
    jr   nz, KEEP
    ld   a, (_fl_want)              ; a reader cell: the last one?
    dec  a
    ld   (_fl_want), a
    jp   z, fl_done
KEEP:
    ld   e, DX
    ld   d, DY
    call fl_enq
SKIP:
    ENDM

    push ix
fl_next:
    ld   a, (_fl_head)
    ld   hl, _fl_tail
    cp   (hl)
    jp   z, fl_done                 ; the queue ran dry
    ld   l, a
    inc  a
    ld   (_fl_head), a
    ld   h, 0
    add  hl, hl
    ld   de, FL_BFSQ
    add  hl, de
    ld   e, (hl)                    ; x
    inc  hl
    ld   d, (hl)                    ; y
    ld   a, e
    ld   (_fl_x), a
    ld   a, d
    ld   (_fl_y), a
    ld   l, d                       ; k = y * 80 + x
    ld   h, 0
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, hl
    ld   b, h
    ld   c, l
    add  hl, hl
    add  hl, hl
    add  hl, bc
    ld   c, e
    ld   b, 0
    add  hl, bc
    push hl
    ld   bc, FL_DIST
    add  hl, bc
    push hl
    pop  ix                         ; IX -> dist[k]
    pop  hl
    ld   bc, _lvl
    add  hl, bc                     ; HL -> lvl[k]
    ld   a, (ix+0)
    inc  a
    cp   FL_TARGET
    jr   nc, fl_next                ; the horizon: labels stay below TARGET
    ld   b, a                       ; B = nd
    ld   c, 0
    ld   a, e
    or   a
    jr   z, fl_f1
    set  0, c                       ; a column to the left
fl_f1:
    cp   FL_MAPW - 1
    jr   z, fl_f2
    set  1, c                       ; a column to the right
fl_f2:
    ld   a, d
    or   a
    jr   z, fl_f3
    set  2, c                       ; a row above
fl_f3:
    cp   FL_MAPH - 1
    jr   z, fl_f4
    set  3, c                       ; a row below
fl_f4:
    bit  2, c
    jp   z, fl_row
    bit  0, c
    jr   z, fl_s1
    FL_TRY -81, -1, -1, fl_s1, fl_k1
    FL_TRY -80, 0, -1, fl_s2, fl_k2
    bit  1, c
    jr   z, fl_s3
    FL_TRY -79, 1, -1, fl_s3, fl_k3
fl_row:
    bit  0, c
    jr   z, fl_s4
    FL_TRY -1, -1, 0, fl_s4, fl_k4
    bit  1, c
    jr   z, fl_s5
    FL_TRY 1, 1, 0, fl_s5, fl_k5
    bit  3, c
    jp   z, fl_next
    bit  0, c
    jr   z, fl_s6
    FL_TRY 79, -1, 1, fl_s6, fl_k6
    FL_TRY 80, 0, 1, fl_s7, fl_k7
    bit  1, c
    jp   z, fl_next
    FL_TRY 81, 1, 1, fl_s8, fl_k8
    jp   fl_next
fl_done:
    pop  ix
    ret

; queue (fl_x + E, fl_y + D) -- if its label (B) is inside the horizon and
; the ring has room (255 entries, as ENQ_OK). Keeps B, C, HL and IX.
fl_enq:
    ld   a, b
    cp   FL_MAXDIST
    ret  nc
    push hl
    ld   a, (_fl_head)
    ld   h, a
    ld   a, (_fl_tail)
    ld   l, a
    sub  h
    inc  a
    jr   z, fl_enq_x                ; 255 queued: the ring is full
    ld   h, 0
    add  hl, hl
    ld   a, l
    add  a, +(FL_BFSQ & 0xff)
    ld   l, a
    ld   a, h
    adc  a, +(FL_BFSQ / 256)
    ld   h, a
    ld   a, (_fl_x)
    add  a, e
    ld   (hl), a
    inc  hl
    ld   a, (_fl_y)
    add  a, d
    ld   (hl), a
    ld   a, (_fl_tail)
    inc  a
    ld   (_fl_tail), a
fl_enq_x:
    pop  hl
    ret
    __endasm;
}
#endif

/* The field is flooded from (sx,sy): the hero, every turn -- or the down
 * stairs, for the title's attract demo (dist_map_from, below).
 *
 * `who` names the monster slots that will READ the field this turn. Their
 * cells are marked TARGET, and the flood stops the moment the last of them
 * is labelled. By then every closer layer is complete, and step_to_hero only
 * ever moves to a LOWER neighbour, so each of them steps exactly as a whole-
 * map field would send it -- the flood just skips whatever lies beyond the
 * farthest reader. That is what pays for flooding the Big Room at all: its
 * whole ring is 1292 cells, where the old queue stopped at 696 and left the
 * rest UNREACH. who == 0 floods everything in reach (the demo reads it all).
 * head/tail run free; the ring slot is their low byte (BFSQ_SIZE 256). */
static void compute_dist_map(uint8_t sx, uint8_t sy, uint16_t who)
{
    uint16_t head = 0, tail = 0;
    uint8_t  want = 0, i;
    uint8_t *d = (uint8_t *)dist;      /* flat view, fast indexing */

#ifdef __ZXNEXT
    { uint16_t k; for (k = 0; k < (uint16_t)(MAPH * MAPW); k++) d[k] = UNREACH; }
#else
    dist_clear(d);
#endif

    if (sx >= MAPW || sy >= MAPH)       /* an int hero_x < 0 casts to >= 128 */
        return;

    for (i = 0; i < mcount; i++)
        if (who & (1u << i)) {
            uint8_t *c = &d[(uint16_t)m_y[i] * MAPW + m_x[i]];
            if (*c == UNREACH) { *c = TARGET; want++; }
        }

    /* queue entries are packed as (y << 8) | x to avoid div/mod on dequeue */
    d[(uint16_t)sy * MAPW + sx] = 0;
    bfsq[(uint8_t)tail++] = (uint16_t)(((uint16_t)sy << 8) | sx);

#ifndef __ZXNEXT
    fl_head = (uint8_t)head; fl_tail = (uint8_t)tail; fl_want = want;
    flood_run();
#else
    while (head != tail) {
        uint16_t    p  = bfsq[(uint8_t)head++];
        uint8_t     cx = (uint8_t)p, cy = (uint8_t)(p >> 8);
        uint16_t    k  = (uint16_t)cy * MAPW + cx;
        uint16_t    kb;                            /* row base: one cell left */
        uint8_t    *dq;
        const char *lq;
        uint8_t     nd = (uint8_t)(d[k] + 1);
        /* The horizon: labels stay below TARGET. The whole-map flood reaches
         * 238 across the maze, so a long enough winding level could get
         * there; cells past it simply stay unset. */
        if (nd >= TARGET) continue;
        if (cy) {                                  /* the row above */
            kb = k - (MAPW + 1); dq = d + kb; lq = (const char *)lvl + kb;
            if (cx)            FLOOD_TRY(0, -257);
                               FLOOD_TRY(1, -256);
            if (cx < MAPW - 1) FLOOD_TRY(2, -255);
        }
        kb = k - 1; dq = d + kb; lq = (const char *)lvl + kb;   /* this row */
        if (cx)                FLOOD_TRY(0, -1);
        if (cx < MAPW - 1)     FLOOD_TRY(2, 1);
        if (cy < MAPH - 1) {                       /* the row below */
            kb = k + (MAPW - 1); dq = d + kb; lq = (const char *)lvl + kb;
            if (cx)            FLOOD_TRY(0, 255);
                               FLOOD_TRY(1, 256);
            if (cx < MAPW - 1) FLOOD_TRY(2, 257);
        }
    }
#endif
}

/* Step one cell down the BFS gradient toward the hero (the lowest-distance free
 * neighbour). Shared by ordinary monsters and by the pet's "follow" mode. */
static void step_to_hero(uint8_t i)
{
    uint8_t bestd = dist[(uint16_t)m_y[i] * MAPW + m_x[i]];   /* our current distance */
    int bestx = -1, besty = -1, dx, dy;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = (int)m_x[i] + dx, ny = (int)m_y[i] + dy;
            uint8_t nd;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            nd = dist[(uint16_t)ny * MAPW + nx];
            if (nd == UNREACH || nd >= bestd) continue;
            {   /* don't stack -- except the pet, which DISPLACES the keeper
                 * (a swapped keeper parked on the door would seal the shop)
                 * and the peaceful townsfolk (a gnome idling in a one-wide
                 * room corked the dog behind it, user-caught in Minetown --
                 * the hero already swaps past peacefuls, so the dog does too) */
                int mj = monster_at(nx, ny);
                if (mj >= 0 &&
                    !(i == pet_idx &&
                      (m_type[mj] == MON_KEEPER || m_peace[mj]))) continue;
            }
            if (i != pet_idx && shop_in_room(nx, ny))
                continue;                            /* shops are an enemy safe zone -- but your pet follows you in */
            bestd = nd; bestx = nx; besty = ny;
        }
    }
    if (bestx >= 0) {
        int mj = monster_at(bestx, besty);
        if (mj >= 0) {                                       /* keeper steps aside */
            mon_face_to((uint8_t)mj, m_x[i]);
            m_x[mj] = m_x[i]; m_y[mj] = m_y[i];
        }
        mon_face_to(i, (uint8_t)bestx);
        m_x[i] = (uint8_t)bestx; m_y[i] = (uint8_t)besty;
    }
}

/* ---- pet AI: bite an adjacent enemy, else heel by the hero ---- */

/* the pet bites enemy ti; the enemy may bite back and the dog can fall */
static void pet_hits(uint8_t pi, uint8_t ti)
{
    const MonType *mt = mon_find(m_type[ti]);
    /* 2..5, +2 per size the dog has grown (4 and 12 lifetime kills --
     * the regen cap in upkeep() grows on the same thresholds) */
    uint8_t dmg = (uint8_t)(rn2(4) + 2 +
                            (pet_kills >= 12 ? 4 : pet_kills >= 4 ? 2 : 0));

    if (m_hp[ti] <= dmg) {
        m_alive[ti] = 0;
        if (dlvl <= MAXLVL)
            mon_dead[dlvl] |= (uint8_t)(m_track & (1u << ti));
        drop_held(ti);                     /* stolen goods return */
        if (rn2(4) == 0)                   /* the dog's kill may leave loot... */
            death_drop(m_x[ti], m_y[ti]);
        else if (rn2(2))                   /* ...or its corpse */
            corpse_drop(m_x[ti], m_y[ti], m_type[ti]);
        msg2("Your dog kills the ", mt->name, "!");
        sfx_kill();
        if (pet_kills < 250) pet_kills++;  /* no hero XP -- the DOG grows */
        if (pet_kills == 4 || pet_kills == 12) {
            pet_hp = (uint8_t)(pet_hp + 6);    /* filling out heals it too */
            msg("Your dog grows bigger!");
        }
        return;
    }
    m_hp[ti] = (uint8_t)(m_hp[ti] - dmg);
    m_sleep[ti] = 0;                         /* the bite wakes it             */
    if (rn2(2)) {                            /* the cornered enemy bites back */
        uint8_t back = (uint8_t)(rn2(mt->dmg) + 1);
        if (pet_hp <= back) {                /* the dog is slain */
            m_alive[pi] = 0; pet_idx = -1; have_pet = 0; pet_hp = 0;
            msg("Your dog dies.");
        } else {
            pet_hp = (uint8_t)(pet_hp - back);
        }
    }
}

/* the dog bites the first adjacent enemy (enemies converge on the hero, so the
 * dog heeling at your side meets them there); 1 if it bit, else 0 */
static uint8_t pet_bite_adjacent(uint8_t i)
{
    uint8_t j;
    for (j = 0; j < mcount; j++) {
        if (!m_alive[j] || j == pet_idx || m_type[j] == MON_KEEPER) continue;
        if (m_type[j] == 'x') continue;    /* the dog can't smell a hidden mimic */
        if (m_peace[j]) continue;          /* it won't maul the townsfolk */
        if (iabs((int)m_x[j] - (int)m_x[i]) <= 1 &&
            iabs((int)m_y[j] - (int)m_y[i]) <= 1) { pet_hits(i, j); return 1; }
    }
    return 0;
}

static void pet_step(uint8_t i)
{
    uint8_t cur;
    if (pet_bite_adjacent(i)) return;
    cur = dist[(uint16_t)m_y[i] * MAPW + m_x[i]];  /* else heel: keep ~2 cells back */
    if (cur == UNREACH || cur > 2) step_to_hero(i);
}

#ifndef __ZXNEXT
/* +zx: heel the dog toward the hero WITHOUT the BFS. When no enemy is near we
 * skip compute_dist_map entirely (its flood over a big room is the 3.5 MHz
 * movement bottleneck), so the pet can't read the distance gradient -- it steps
 * greedily by line of sight instead. Open rooms (where speed actually matters)
 * route fine; in a corridor tangle the dog may briefly lag, then rejoins once an
 * enemy wakes the real chase, or on the next level (place_pet). */
static uint8_t pet_heel_greedy(uint8_t i)   /* 1 = heeled/stepped, 0 = boxed in */
{
    int hx = hero_x, hy = hero_y;
    int dh = iabs(hx - (int)m_x[i]), dv = iabs(hy - (int)m_y[i]);
    int bestc, dx, dy, bestx = -1, besty = -1;
    if (dh <= 2 && dv <= 2) {
        /* nominally at heel -- but Chebyshev ignores WALLS: parked one wall
         * away (dog inside the shop, hero just outside its second door) the
         * dog believed it was beside you forever. Heel only counts when we
         * are adjacent or the one step toward you is open floor; otherwise
         * fall through -- the greedy finds nothing strictly closer, returns
         * 0, and the caller's BFS routes us around through the door. */
        char t;
        if (dh <= 1 && dv <= 1) return 1;
        t = lvl[(int)m_y[i] + ((hy > (int)m_y[i]) - (hy < (int)m_y[i]))]
               [(int)m_x[i] + ((hx > (int)m_x[i]) - (hx < (int)m_x[i]))];
        if (t != '|' && t != '-' && t != ' ') return 1;
    }
    bestc = (dh > dv) ? dh : dv;                /* current Chebyshev distance     */
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = (int)m_x[i] + dx, ny = (int)m_y[i] + dy, a, b, c, mj;
            char t;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            a = iabs(hx - nx); b = iabs(hy - ny);   /* distance filter FIRST (cheap) */
            c = (a > b) ? a : b;
            if (c >= bestc) continue;
            t = lvl[ny][nx];
            if (t == '|' || t == '-' || t == ' ') continue;   /* wall/rock     */
            if (nx == hx && ny == hy) continue;               /* not onto hero */
            mj = monster_at(nx, ny);
            if (mj >= 0 && m_type[mj] != MON_KEEPER &&
                !m_peace[mj]) continue;  /* don't stack -- but the pet
                                 * DISPLACES the keeper AND the peaceful
                                 * townsfolk (below): a keeper parked on the
                                 * shop door would seal the dog in or out,
                                 * and an idling gnome in a one-wide room
                                 * corked the dog behind it (user-caught) */
            bestc = c; bestx = nx; besty = ny;
        }
    }
    if (bestx >= 0) {
        int mj = monster_at(bestx, besty);
        if (mj >= 0) {                                        /* keeper steps aside */
            mon_face_to((uint8_t)mj, m_x[i]);
            m_x[mj] = m_x[i]; m_y[mj] = m_y[i];
        }
        mon_face_to(i, (uint8_t)bestx);
        m_x[i] = (uint8_t)bestx; m_y[i] = (uint8_t)besty;
        return 1;
    }
    return 0;                          /* boxed in: caller floods once and routes */
}

/* +zx: an enemy chases the hero greedily by line of sight (no BFS). In the open
 * room where it matters for speed this always finds a closer cell, so the flood
 * never runs -- that flood over a big room is why a single visible monster lagged
 * the whole room. Returns 0 only when a wall boxes every closer step, leaving the
 * BFS to route it (corridor floods are small). The caller handles adjacency. */
static uint8_t enemy_chase_greedy(uint8_t i)
{
    int hx = hero_x, hy = hero_y;
    int dh = iabs(hx - (int)m_x[i]), dv = iabs(hy - (int)m_y[i]);
    int curc = (dh > dv) ? dh : dv;                /* must beat this Chebyshev */
    int bestc = 0, bestm = 0, dx, dy, bestx = -1, besty = -1;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = (int)m_x[i] + dx, ny = (int)m_y[i] + dy, a, b, c, m;
            char t;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            /* distance filters FIRST: they reject most neighbours for a couple of
             * adds, so the pricier probes below run for ~2 real candidates, not 8 */
            a = iabs(hx - nx); b = iabs(hy - ny);
            c = (a > b) ? a : b;
            if (c >= curc) continue;                          /* must get closer    */
            m = a + b;            /* Manhattan tiebreak: among equal Chebyshev, the
                                   * straighter step -- else it drifts diagonally   */
            if (bestx >= 0 && (c > bestc || (c == bestc && m >= bestm)))
                continue;                                     /* not better anyway  */
            t = lvl[ny][nx];
            if (t == '|' || t == '-' || t == ' ') continue;   /* wall/rock         */
            if (nx == hx && ny == hy) continue;               /* attack is separate */
            if (shop_in_room(nx, ny)) continue;               /* shops are a safe zone */
            if (monster_at(nx, ny) >= 0) continue;            /* don't stack         */
            bestc = c; bestm = m; bestx = nx; besty = ny;
        }
    }
    if (bestx >= 0) {
        mon_face_to(i, (uint8_t)bestx);
        m_x[i] = (uint8_t)bestx; m_y[i] = (uint8_t)besty;
        return 1;
    }
    return 0;
}
#endif

/* a monster steps to the neighbour closest to the hero (lowest distance) */
static void mon_step(uint8_t i)
{
    int ddx, ddy;

    if (m_type[i] == MON_KEEPER) return;   /* the shopkeeper never moves */
    if (m_type[i] == 'e') return;          /* the floating eye just floats */
    if (still_asleep(i)) return;           /* a spawn sleeper, undisturbed */
    if (m_sleep[i]) { m_sleep[i]--; return; }    /* asleep (wand of sleep): no turn */
    if (i == pet_idx) { pet_step(i); return; }   /* the pet follows its own rules */
    if (m_peace[i]) { peace_amble(i); return; }  /* a peaceful minds its own */
    if (m_blind[i]) { m_blind[i]--; peace_amble(i); return; }  /* it cannot find you */

    ddx = hero_x - (int)m_x[i];
    ddy = hero_y - (int)m_y[i];

    if (iabs(ddx) <= 1 && iabs(ddy) <= 1) {   /* adjacent -> attack */
        if (ward) return;                     /* Elbereth or the scroll: it dares
                                               * not strike */
        monster_hits_player(i);
        return;
    }

    step_to_hero(i);
}

/* A dragon with a clear straight line to the hero (range 6, same row or
 * column, no wall/door/monster between) may breathe fire instead of stepping:
 * rn2(8)+5, half-soaked by armour. 1 = it breathed (its turn is spent). */
static uint8_t dragon_breath(uint8_t i)
{
    int dx, dy, sx, sy;
    if (m_type[i] != 'D' || m_blind[i]) return 0;
    if (m_sleep[i]) return 0;              /* a sleeping dragon only snores */
    dx = hero_x - (int)m_x[i];
    dy = hero_y - (int)m_y[i];
    if (iabs(dx) <= 1 && iabs(dy) <= 1) return 0;   /* adjacent: bite instead */
    if (dx != 0 && dy != 0) return 0;               /* straight lines only    */
    if (iabs(dx + dy) > 6) return 0;                /* out of range           */
    if (rn2(2)) return 0;                           /* it inhales...          */
    sx = dx ? (dx > 0 ? 1 : -1) : 0;
    sy = dy ? (dy > 0 ? 1 : -1) : 0;
    {   int x = (int)m_x[i] + sx, y = (int)m_y[i] + sy;
        while (x != hero_x || y != hero_y) {
            char c = terrain(x, y);
            if (!walkable(c) || c == '+') return 0; /* walls and doors block  */
            if (monster_at(x, y) >= 0) return 0;    /* something shields you  */
            x += sx; y += sy;
        }
    }
    {   uint8_t dmg  = (uint8_t)(rn2(8) + 5);
        uint8_t soak = (uint8_t)(armor_def >> 1);   /* armour half-shields it */
        dmg = (dmg > soak) ? (uint8_t)(dmg - soak) : 1;
        sfx_hurt();
        if (php <= dmg) {
            php = 0; dead = 1;
            msg("The dragon's fire engulfs you!");
        } else {
            php = (uint8_t)(php - dmg);
            msg("The dragon breathes fire!");
        }
    }
    return 1;
}

/* A hidden mimic ('x') holds its pose until the hero steps next to the "item";
 * then it sheds the disguise and acts at once. 1 = still hidden, skip it. */
static uint8_t mimic_hidden(uint8_t i)
{
    if (m_type[i] != 'x') return 0;
    if (iabs((int)m_x[i] - hero_x) <= 1 && iabs((int)m_y[i] - hero_y) <= 1) {
        m_type[i] = 'm';
        msg("The mimic reveals itself!");
        return 0;
    }
    return 1;
}

void monsters_turn(void) __banked
{
    uint8_t i;
    /* trolls knit their wounds shut as they come for you (cap = spawn HP) --
     * unless Trollsbane is in your hand */
    if (!(art_fx & AF_TROLLS))
        for (i = 0; i < mcount; i++)
            if (m_alive[i] && m_type[i] == 'T') {
                uint8_t cap = (uint8_t)(mon_find('T')->hp + eff_depth() / 2);
                if (m_hp[i] < cap) m_hp[i]++;
            }
    sting_glow();
    ward = (uint8_t)((el_life && hero_x == el_x && hero_y == el_y) ||
                     item_scare_here());
#ifndef __ZXNEXT
    /* +zx: skip the whole chase when no ENEMY is near. The pet (always at your
     * heel) and the stationary shopkeeper must NOT count here -- otherwise the
     * BFS would flood every single turn, which is exactly what made big rooms
     * crawl once the dog arrived (v0.4.0). With no enemy awake we only heel the
     * dog, greedily, and skip compute_dist_map. */
    uint8_t awake = 0;
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i] || i == pet_idx || m_type[i] == MON_KEEPER) continue;
        if (m_type[i] == 'x') {
            /* A posing mimic wakes no CHASE at a distance -- that is the whole
             * point of the disguise, and flooding for it would cost the speed
             * this early-out buys. But one at your elbow is about to spring,
             * and the loop below is what reveals and bites: without this the
             * mimic sat inert on a quiet level (and since 0.11 spawns half the
             * dungeon asleep, quiet IS the common case). */
            if (iabs((int)m_x[i] - hero_x) <= 1 &&
                iabs((int)m_y[i] - hero_y) <= 1) awake = 1;
            continue;
        }
        if (m_peace[i] || m_blind[i]) continue;   /* townsfolk and the blind
                                                      * trigger no chase */
        if (still_asleep(i)) continue;     /* spawn sleepers roll their one
                                            * per-turn wake chance HERE (no
                                            * break: every sleeper rolls);
                                            * asleep = wakes nothing */
        if (iabs((int)m_x[i] - hero_x) <= MON_WAKE &&
            iabs((int)m_y[i] - hero_y) <= MON_WAKE) awake = 1;
    }
    if (!awake) {
        /* the townsfolk still mill about (cheap: no BFS involved) */
        for (i = 0; i < mcount; i++)
            if (m_alive[i] && (m_peace[i] || m_blind[i])) {
                if (m_blind[i]) m_blind[i]--;   /* the flash wears off */
                peace_amble(i);
            }
        /* no enemy near: heel the dog by sight, no flood. Only when a wall boxes
         * the greedy step (a bend or doorway it can't round straight to you) do we
         * flood -- once, routing just the dog. An open room never blocks, so it
         * never pays; a corridor's flood is small. This keeps the dog ~2 cells back
         * everywhere instead of letting the gap grow turn by turn through bends. */
        if (pet_idx < 0 || !m_alive[(uint8_t)pet_idx]) return;
        if (pet_heel_greedy((uint8_t)pet_idx)) return;
        compute_dist_map((uint8_t)hero_x, (uint8_t)hero_y,
                         (uint16_t)(1u << (uint8_t)pet_idx));   /* for the dog alone */
        pet_step((uint8_t)pet_idx);
        return;
    }
    /* an enemy IS near: every monster chases GREEDILY by line of sight (no flood).
     * The BFS runs only for any that a wall boxes in -- an open room never blocks,
     * so a single visible monster no longer floods the whole big room each turn,
     * which is what made it lag. */
    {
        uint16_t blocked = 0;
        for (i = 0; i < mcount; i++) {
            if (!m_alive[i] || m_type[i] == MON_KEEPER) continue;
            if (m_type[i] == 'e') continue;   /* the floating eye just floats */
            if (mimic_hidden(i)) continue;    /* posing as an item */
            if (m_sleep[i] == 255) continue;  /* still asleep (rolled in the scan) */
            if (m_sleep[i]) { m_sleep[i]--; continue; }
            if (m_peace[i]) { peace_amble(i); continue; }  /* minds its own */
            if (m_blind[i]) { m_blind[i]--; peace_amble(i); continue; }
            if (i == (uint8_t)pet_idx) {
                if (pet_bite_adjacent(i)) continue;
                if (!pet_heel_greedy(i)) blocked |= (uint16_t)(1u << i);
                continue;
            }
            if (iabs((int)m_x[i] - hero_x) > MON_WAKE ||
                iabs((int)m_y[i] - hero_y) > MON_WAKE) continue;   /* still dormant */
            if (iabs(hero_x - (int)m_x[i]) <= 1 && iabs(hero_y - (int)m_y[i]) <= 1) {
                if (ward) continue;   /* Elbereth, or scare monster underfoot */
                monster_hits_player(i);
                if (dead) return;
                continue;
            }
            if (dragon_breath(i)) {           /* fire outranges Elbereth */
                if (dead) return;
                continue;
            }
            if (!enemy_chase_greedy(i)) blocked |= (uint16_t)(1u << i);
        }
        if (blocked) {                          /* route only the wall-boxed ones */
            compute_dist_map((uint8_t)hero_x, (uint8_t)hero_y, blocked);
            for (i = 0; i < mcount; i++) {
                if (!(blocked & (uint16_t)(1u << i)) || !m_alive[i]) continue;
                if (i == (uint8_t)pet_idx) pet_step(i);
                else                       step_to_hero(i);
            }
        }
    }
    return;
#endif
    {   /* Who reads the field this turn -- mon_step's own rules, so the flood
         * can stop at the farthest of them (compute_dist_map) or not run at
         * all. A spawn sleeper can only stir within 5 cells, or under
         * aggravate monster (still_asleep); a wand's doze just counts down;
         * the keeper, the eye, a posing mimic and the peaceful never chase. */
        uint16_t who = 0;
        for (i = 0; i < mcount; i++) {
            uint8_t s = m_sleep[i];
            char    t = m_type[i];
            if (!m_alive[i] || m_peace[i] || m_blind[i] || t == MON_KEEPER ||
                t == 'e' || t == 'x')
                continue;
            if (s == 255 && !(ring_fx & RF_AGGR) &&
                (iabs((int)m_x[i] - hero_x) > 5 || iabs((int)m_y[i] - hero_y) > 5))
                continue;
            if (s && s != 255) continue;
            who |= (uint16_t)(1u << i);
        }
        if (who) compute_dist_map((uint8_t)hero_x, (uint8_t)hero_y, who);
    }
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i]) continue;
        if (mimic_hidden(i)) continue;    /* posing as an item */
        if (dragon_breath(i)) {           /* fire outranges Elbereth */
            if (dead) return;
            continue;
        }
        mon_step(i);
        if (dead) return;
    }
}

/* maybe_spawn_wanderer (the per-turn wandering-monster roll) moved to
 * monster_spawn.c with the rest of the spawning. */

/* ---- the title's attract demo (attract.c) ----
 * The demo hero walks to the down stairs down the same field a monster walks
 * to you: flooded once from the stairs, then read a cell at a time. It is only
 * ever run while no game is, so borrowing the per-turn field is free; on the
 * 128K it stops at MAXDIST like the chase's, which is why the demo starts its
 * hero within that reach. dist[] stays private to this file -- the demo
 * reads it by value. */
void dist_map_from(uint8_t x, uint8_t y) __banked { compute_dist_map(x, y, 0); }

uint8_t dist_at(uint8_t x, uint8_t y) __banked
{
    return dist[(uint16_t)y * MAPW + x];
}

/* No hostile turn runs in the demo -- nobody fights there -- so every awake
 * monster mills about like a townsman instead. Sleepers sleep on, the keeper
 * keeps his shop, the eye floats and the mimic holds its pose; the demo walks
 * the pet itself. */
void monsters_amble(void) __banked
{
    uint8_t i;
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i] || (int8_t)i == pet_idx || m_sleep[i]) continue;
        if (m_type[i] == MON_KEEPER || m_type[i] == 'e' || m_type[i] == 'x')
            continue;
        peace_amble(i);
    }
}
