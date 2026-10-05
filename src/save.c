/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* save.c - save & restore: NetHack-style save & quit, consumed on load.
 *
 * WHY IT IS ITS OWN FILE. It was the top of nexthack.c until 2026-09-27. The
 * field-by-field copies below are ~1.3 KB of code and prompt text, nexthack.c
 * sits in the indivisible nexthack+classes+spells colocate group -- a few
 * hundred bytes from full on the 128K -- and this code had to GROW. Nothing
 * here is pointer-coupled to that group: every global it copies is resident
 * DATA, its literals go only to the resident print_str, and each module's
 * block is reached through that module's __banked *_save/*_load. So it may
 * live in any bank with room (banks.json).
 *
 * THE FORMAT (SAVE_VER 30): a header, the player block, door_open, then the
 * item, level and monster blocks, then a 4-byte trailer -- the count and the
 * 16-bit sum of every byte before it, which file_write keeps (platform.c).
 *
 * WHAT A LOAD TRUSTS: nothing, until the whole file has been read through
 * once (save_check) and its length and sum agree with that trailer. The old
 * load read each block straight into the live globals and deleted the file
 * afterwards, so a save cut short by a full or failing card came back as a
 * game built from whatever the stack and the last run left in RAM -- an
 * inventory count or a dlvl nothing else checks. Now a bad save is refused
 * before any of it reaches the game, and the player is asked about it just
 * as about another version's: the file is theirs.
 */

#include "game.h"
#include "platform.h"
#include "rng.h"
#include "level.h"
#include "monster.h"
#include "item.h"
#include "nexthack.h"

#define SAVE_NAME  "nexthack.sav"
#define SAVE_MAGIC 0x484Eu          /* 'N','H' */
#define SAVE_VER   34     /* 34: the classes -- the Tourist's camera makes 81
                           * types (id_known 11 bytes). 33: the tools --
                           * levels and shops place them, so
                           * a level's loot (and the kill masks after it)
                           * would no longer match an older save's. 32: the
                           * weapon ladder -- 75 types, id_known 10 bytes. 31: the artifacts -- art_given joined the
                           * player block, and 68+ types grew id_known to 9.
                           * 30 was 1.5's: the play RNG's state joined the
                           * player block and a length+sum trailer closes the
                           * file. 29 was
                           * 1.4's (the item batch: 60 types, id_known 8 bytes,
                           * the genocide mask); 28 1.3's; 27 the 1.0 freeze.
                           * An older save gets the 1.3.1 prompt. */

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
    uint16_t pray_timeout;
    uint8_t  have_pet, pet_hp, pet_kills;
    uint8_t  luckstone_taken;
    uint16_t cnt_kills, cnt_corpses, cnt_reads, cnt_prayers;
    uint8_t  at_str, at_dex, at_con, at_int, at_wis, at_cha;
    uint8_t  pclass, intrinsics, pw, pmaxpw;
    uint8_t  known_spells;
    uint16_t max_dlvl;
    uint8_t  alignment;
    int8_t   luck;
    uint16_t rng;         /* the PLAY stream, so a restore picks up the dice
                           * where the save left them (build_level no longer
                           * rewinds it either -- see nexthack_lvl.c) */
    uint8_t  art_given;   /* which artifacts exist: each is unique */
};

/* Write seed + player + each module's state. Returns 1 on success. */
int save_game(void) __banked
{
    uint8_t h = file_create(SAVE_NAME);
    struct save_hdr    hdr;
    struct save_player p;
    uint16_t trail[2];

    if (h == FILE_ERR) return 0;
    file_bad = 0; file_len = 0; file_sum = 0;

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
    p.pray_timeout = pray_timeout;
    p.have_pet = have_pet; p.pet_hp = pet_hp; p.pet_kills = pet_kills;
    p.luckstone_taken = luckstone_taken;
    p.cnt_kills = cnt_kills; p.cnt_corpses = cnt_corpses;
    p.cnt_reads = cnt_reads; p.cnt_prayers = cnt_prayers;
    p.at_str = at_str; p.at_dex = at_dex; p.at_con = at_con;
    p.at_int = at_int; p.at_wis = at_wis; p.at_cha = at_cha;
    p.pclass = pclass; p.intrinsics = intrinsics;
    p.pw = pw; p.pmaxpw = pmaxpw;
    p.known_spells = known_spells;
    p.max_dlvl = max_dlvl;
    p.alignment = alignment;
    p.luck = luck;
    p.rng = rng_get();
    p.art_given = art_given;
    file_write(h, &p, sizeof p);

    file_write(h, door_open, sizeof door_open);
    item_save(h);
    level_save(h);
    monster_save(h);
    trail[0] = file_len; trail[1] = file_sum;   /* taken before they count themselves */
    file_write(h, trail, sizeof trail);
    file_close(h);
    /* A short write -- a full or write-protected card -- used to be reported
     * as saved, and the title then loaded the stump. Now the stump goes and
     * the caller says so; the run is still in RAM and play goes on. */
    if (file_bad) { file_remove(SAVE_NAME); return 0; }
    return 1;
}

#define SV_OK      0
#define SV_NONE    1      /* no save on the card (or no card)       */
#define SV_VERSION 2      /* another version's: the 1.3.1 prompt    */
#define SV_DAMAGED 3      /* cut short, or its sum does not match   */

/* The first pass of a load: read the file through once, keeping nothing.
 * The header must be this version's; then every byte goes into a count and
 * a sum while the last four -- the trailer -- are held back in a window, and
 * the two must agree with it at the end. */
static uint8_t save_check(void)
{
    uint8_t  buf[32], last[4], h, i;
    uint16_t got, len = 0, sum = 0;

    h = file_open(SAVE_NAME);
    if (h == FILE_ERR) return SV_NONE;
    got = file_read(h, buf, sizeof buf);
    if (got < sizeof(struct save_hdr)) { file_close(h); return SV_DAMAGED; }
    if (((struct save_hdr *)buf)->magic != SAVE_MAGIC ||
        ((struct save_hdr *)buf)->ver   != SAVE_VER) { file_close(h); return SV_VERSION; }
    last[0] = last[1] = last[2] = last[3] = 0;
    do {
        for (i = 0; i < (uint8_t)got; i++) {
            sum += last[0];              /* the byte leaving the window is payload */
            last[0] = last[1]; last[1] = last[2]; last[2] = last[3];
            last[3] = buf[i];
        }
        len += got;
    } while ((got = file_read(h, buf, sizeof buf)) != 0);
    file_close(h);
    if (len < 4) return SV_DAMAGED;
    return ((uint16_t)(last[0] | (last[1] << 8)) == (uint16_t)(len - 4) &&
            (uint16_t)(last[2] | (last[3] << 8)) == sum) ? SV_OK : SV_DAMAGED;
}

/* A save that cannot be loaded is named, and the player asked: the file is
 * theirs, not ours. n leaves it on the card -- for the older binary that
 * wrote it, or, if it is damaged, for a card that misread once and may read
 * it next time. (Before 1.3.1 another version's save was deleted without a
 * word; before this, a damaged one was loaded.) */
static void save_refused(uint8_t why)
{
    int k;
    tm_cls();
#ifdef __ZXNEXT
    if (why == SV_VERSION) {
        print_str(20,  9, "This saved game is from another", C_WHITE | C_BRIGHT);
        print_str(20, 10, "version and cannot be loaded.",   C_WHITE | C_BRIGHT);
        print_str(20, 14, "n keeps the file for the older",  C_CYAN | C_BRIGHT);
        print_str(20, 15, "version you saved it with.",      C_CYAN | C_BRIGHT);
    } else {
        print_str(20,  9, "This saved game is damaged or",   C_WHITE | C_BRIGHT);
        print_str(20, 10, "cut short and cannot be loaded.", C_WHITE | C_BRIGHT);
        print_str(20, 14, "n keeps the file: a card that",   C_CYAN | C_BRIGHT);
        print_str(20, 15, "misread once may read it later.", C_CYAN | C_BRIGHT);
    }
    print_str(20, 12, "Delete it and start fresh?  y/n", C_YELLOW | C_BRIGHT);
#else
    if (why == SV_VERSION) {
        print_str(1,  6, "This saved game is from",      C_WHITE | C_BRIGHT);
        print_str(1,  7, "another version and cannot",   C_WHITE | C_BRIGHT);
        print_str(1, 13, "n keeps it for the version",   C_CYAN | C_BRIGHT);
        print_str(1, 14, "that wrote it.",               C_CYAN | C_BRIGHT);
    } else {
        print_str(1,  6, "This saved game is damaged",   C_WHITE | C_BRIGHT);
        print_str(1,  7, "or cut short and cannot",      C_WHITE | C_BRIGHT);
        print_str(1, 13, "n keeps it: a card that",      C_CYAN | C_BRIGHT);
        print_str(1, 14, "misread may read it later.",   C_CYAN | C_BRIGHT);
    }
    print_str(1,  8, "be loaded.",                   C_WHITE | C_BRIGHT);
    print_str(1, 10, "Delete it and start fresh?",   C_YELLOW | C_BRIGHT);
    print_str(1, 11, "y / n",                        C_YELLOW | C_BRIGHT);
#endif
    in_wait_nokey();
    do { k = getkey(); } while (k != 'y' && k != 'Y' && k != 'n' && k != 'N');
    in_wait_nokey();
    if (k == 'y' || k == 'Y') file_remove(SAVE_NAME);
    map_dirty = 1;              /* the prompt drew over the playfield */
}

/* Load a saved game and delete the file (so it cannot be reloaded - the
 * NetHack anti-save-scum rule). Returns 1 if a valid save was restored; on 0
 * nothing of the game in RAM has been touched. */
int load_game(void) __banked
{
    struct save_hdr    hdr;
    struct save_player p;
    uint8_t h, v = save_check();

    if (v == SV_NONE) return 0;
    if (v != SV_OK) { save_refused(v); return 0; }

    /* The second pass reads what the first has just proved whole. */
    h = file_open(SAVE_NAME);
    if (h == FILE_ERR) return 0;
    file_read(h, &hdr, sizeof hdr);
    file_read(h, &p, sizeof p);
    world_seed = p.world_seed;
    hero_x = p.hero_x; hero_y = p.hero_y;
    dlvl = p.dlvl;     turns = p.turns;
    php = p.php;       pmaxhp = p.pmaxhp;
    gold = p.gold;     nutrition = p.nutrition;
    xp = p.xp;         xlvl = p.xlvl; has_amulet = p.has_amulet;
    st_conf = p.st_conf;   st_blind = p.st_blind;
    st_sleep = p.st_sleep; st_poison = p.st_poison;
    pray_timeout = p.pray_timeout;
    have_pet = p.have_pet; pet_hp = p.pet_hp; pet_kills = p.pet_kills;
    luckstone_taken = p.luckstone_taken;
    cnt_kills = p.cnt_kills; cnt_corpses = p.cnt_corpses;
    cnt_reads = p.cnt_reads; cnt_prayers = p.cnt_prayers;
    at_str = p.at_str; at_dex = p.at_dex; at_con = p.at_con;
    at_int = p.at_int; at_wis = p.at_wis; at_cha = p.at_cha;
    pclass = p.pclass; intrinsics = p.intrinsics;
    pw = p.pw; pmaxpw = p.pmaxpw;
    known_spells = p.known_spells;
    max_dlvl = p.max_dlvl;
    alignment = p.alignment;
    luck = p.luck;
    art_given = p.art_given;
    rng_set(p.rng);             /* main's build_level keeps it (nexthack_lvl.c) */
    dead = 0; won = 0;

    file_read(h, door_open, sizeof door_open);
    item_load(h);
    level_load(h);
    monster_load(h);
    file_close(h);
    file_remove(SAVE_NAME);
    return 1;
}
