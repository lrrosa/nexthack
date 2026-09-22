/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* item_use.c - the verbs that ACTIVATE or CONSUME an item: quaff (and drink
 * from a fountain), eat (and the corpse effects), read, throw, zap.
 *
 * Split out of item.c when its bank filled: PAGE_28 was down to 163 B on the
 * Next and BANK_0 to 240 B on the 128K, and item.c sat alone in both, so no
 * relocation could help -- the same shape as nexthack_lvl.c. These were the
 * right cut because none of them needs a POINTER into item.c's catalogue. They
 * print with their own literals, and what they need from the catalogue is a
 * value (a prop, a class) that crosses by item_obj_prop/item_obj_cls. The item
 * menu they open, select_item, stays in item.c beside the names it prints, and
 * derives its own prompt from the class so no literal of ours crosses into it.
 * The whole rule, and the API, are in item_int.h.
 *
 * Nothing else changed about these functions: their calls into spells.c,
 * monster_ai.c and nexthack_lvl.c were already cross-bank before the move. */

#include "item.h"
#include "item_int.h"    /* obj_t, inv, O_* ids, and item.c's __banked API    */
#include "game.h"        /* hero_x/y, php, pmaxhp, weapon_dmg, armor_def, ac */
#include "platform.h"    /* drawing, messages, getkey                        */
#include "level.h"       /* terrain, level_random_floor                      */
#include "monster.h"     /* monster_at, hit_monster, m_sleep, pet_idx        */
#include "spells.h"      /* learn_spell ('r' on a spellbook)                 */
#include "nexthack.h"    /* build_level (wand of digging descends a level)   */
#include "rng.h"         /* rn2, world_seed                                  */
#include "sfx.h"         /* sound effects                                    */

/* Drink from the fountain the hero stands on. A worthy blade may draw
 * Excalibur from the depths (NetHack's dip, folded into the quaff); else the
 * water blesses or curses at random. Sets acted/turns itself. */
static void quaff_fountain(void)
{
    uint8_t i;
    acted = 1; turns++;

    /* the Lady of the Lake: a Valkyrie of level 5+ wielding a plain long sword
     * may draw Excalibur (once -- the sword becomes the artifact) */
    if (pclass == 0 && xlvl >= 5) {
        for (i = 0; i < inv_count; i++) {
            if (inv[i].worn && inv[i].otyp == O_LONGSW) {
                if (rn2(3) == 0) {
                    inv[i].otyp = O_EXCALIBUR;
                    inv[i].buc = BUC_BLESS | BUC_KNOWN;
                    inv[i].ero = 0;
                    item_id_set(O_EXCALIBUR);
                    item_recompute_gear();
                    msg("A hand offers up Excalibur!");
                    return;
                }
                break;
            }
        }
    }

    switch (rn2(6)) {
    case 0: case 1:                      /* cool, clear water */
        php = (uint8_t)(php + rn2(5) + 2);
        if (php > pmaxhp) php = pmaxhp;
        msg("The water is cool and clear.");
        break;
    case 2:                              /* murky water */
        if (intrinsics & INTR_POISON_RES) { msg("This water tastes stale."); }
        else { st_poison = (uint8_t)(st_poison + rn2(4) + 3);
               msg("Yecch!  Foul, murky water."); }
        break;
    case 3:                              /* coins glinting at the bottom */
        { uint16_t amt = (uint16_t)(rn2(30) + 5);
          if (gold > (uint16_t)(60000u - amt)) gold = 60000u;  /* clamp, 16-bit */
          else                                 gold = (uint16_t)(gold + amt);
          msg_num("You scoop up ", amt, " gold pieces."); }
        break;
    case 4:                              /* the fountain dries up */
        lvl[hero_y][hero_x] = '.';
        map_flush = 1;                   /* +zx: the '{' cell changed */
        msg("The fountain dries up!");
        break;
    default:
        msg("The water tastes flat.");
        break;
    }
}

/* A potion of gain ability: +1 to one attribute at random -- blessed, all
 * six -- capped at 18, with NetHack's lines. Cursed does nothing; a floor
 * potion is never cursed, but an altar can make one so, and bless one too
 * (drop_at_feet), which is how the blessed kind is had. */
static void gain_ability(uint8_t buc)
{
    static uint8_t *const at[6] = { &at_str, &at_dex, &at_con,
                                    &at_int, &at_wis, &at_cha };
    static const char *const feel[6] = {
        "You feel strong!", "You feel agile!", "You feel tough!",
        "You feel smart!",  "You feel wise!",  "You feel charismatic!"
    };
    uint8_t i, k, n = 0;
    if (buc != BUC_CURSE) {
        if (buc == BUC_BLESS) {
            for (i = 0; i < 6; i++)
                if (*at[i] < 18) { (*at[i])++; n++; }
            if (n) { msg("You feel better in every way!"); return; }
        } else {
            k = (uint8_t)rn2(6);            /* a random one that has room */
            for (i = 0; i < 6; i++, k = (uint8_t)(k == 5 ? 0 : k + 1))
                if (*at[k] < 18) { (*at[k])++; msg(feel[k]); return; }
        }
    }
    msg("You feel no different.");
}

void do_quaff(void) __banked
{
    int s;
    uint8_t ot, buc;

    if (lvl[hero_y][hero_x] == '{') {    /* standing on a fountain: drink it? */
        int k;
        msg("Drink from the fountain? y/n");
        in_wait_nokey();
        k = getkey();
        in_wait_nokey();
        if (k == 'y' || k == 'Y') { quaff_fountain(); return; }
        /* else fall through to drinking a carried potion */
    }

    s = select_item('!');
    if (s == -1) { msg("You have no potions to drink."); return; }
    if (s == -2) { msg("Never mind."); return; }
    ot  = inv[s].otyp;
    buc = (uint8_t)buc_st(&inv[s]);
    if (ot == O_CONFUSION) {
        st_conf = (uint8_t)(st_conf + rn2(15) + 15);
        msg("Huh?  What?  Where am I?");
    } else if (ot == O_SLEEPING) {
        if (intrinsics & INTR_SLEEP_RES) {
            msg("You yawn.");           /* sleep resistance shrugs it off */
        } else {
            st_sleep = (uint8_t)(st_sleep + rn2(8) + 5);
            msg("You suddenly fall asleep!");
        }
    } else if (ot == O_BLINDNESS) {
        st_blind = (uint8_t)(st_blind + rn2(40) + 30);
        map_dirty = 1;                      /* redraw: the world goes dark */
        msg("Darkness falls around you.");
    } else if (ot == O_GAINLVL) {
        msg("You feel more experienced!");
        level_up();     /* monster_ai owns the XP curve ("Welcome to...") */
    } else if (ot == O_PGAINABIL) {
        gain_ability(buc);
    } else if (ot == O_PGAINENRG) {
        /* NetHack's +d5-ish to the pool, and the pool refilled; 60 is the
         * cap the altar's boon already keeps */
        uint8_t g = (uint8_t)(rn2(4) + 2);
        pmaxpw = (uint8_t)((uint16_t)pmaxpw + g > 60 ? 60 : pmaxpw + g);
        pw = pmaxpw;
        msg("Magic courses through you!");
    } else {                                /* healing / extra healing */
        uint8_t heal = (uint8_t)(rn2(6) + item_obj_prop(ot));
        if (ot == O_EXHEAL) {
            if (pmaxhp < 250) pmaxhp++;
            msg("You feel much healthier!");
        } else {
            msg("You feel much better.");
        }
        php = (uint8_t)(php + heal);
        if (php > pmaxhp) php = pmaxhp;
    }
    item_id_set(ot);                 /* drinking it identifies the type */
    item_inv_remove((uint8_t)s);
    sfx_quaff();
    acted = 1; turns++;
}

/* "You are what you eat": a corpse feeds less than a ration but some flesh
 * teaches the body something -- or punishes it. The NetHack classics. */
static void eat_corpse(char mch)
{
    if (mch == 'S' || mch == 'k') {              /* poisonous flesh */
        if (intrinsics & INTR_POISON_RES)
            msg("Ecch.  No harm done.");
        else if (rn2(3) == 0) {
            intrinsics |= INTR_POISON_RES;
            msg("You feel healthy!");
        } else {
            st_poison = (uint8_t)(st_poison + 8);
            msg("Ecch - that was poisonous!");
        }
    } else if (mch == 'i') {                     /* homunculus: sleepy flesh */
        if (intrinsics & INTR_SLEEP_RES)
            msg("You eat.  Chewy.");
        else if (rn2(2)) {
            intrinsics |= INTR_SLEEP_RES;
            msg("You feel wide awake!");
        } else {
            st_sleep = (uint8_t)(st_sleep + rn2(4) + 3);
            msg("You doze off...");
        }
    } else if (mch == 'e') {                     /* floating eye: the classic */
        intrinsics |= INTR_TELEPATHY;
        map_dirty = 1;               /* if already blind, sense them at once */
        msg("You feel a strange awareness!");
    } else if (mch == 'a') {                     /* acid blob: burns going down */
        if (php > 2) php = (uint8_t)(php - 2);
        msg("Acrid!  It burns.");
    } else if (mch == 'D') {                     /* dragon flesh hardens the blood */
        intrinsics |= INTR_POISON_RES;
        msg("You feel healthy!");
    } else {
        msg("You eat.  Not bad.");
    }
}

void do_eat(void) __banked
{
    int s = select_item('%');
    if (s == -1) { msg("You have nothing to eat."); return; }
    if (s == -2) { msg("Never mind."); return; }
    if (nutrition > 1200) {
        msg("You are too full to eat now.");
        return;
    }
    if (inv[s].otyp == O_CORPSE) {
        char mch = (char)inv[s].ench;
        item_inv_remove((uint8_t)s);
        nutrition += 250;                    /* lean fare next to a ration */
        if (nutrition > 1500) nutrition = 1500;
        cnt_corpses++;              /* flesh breaks Vegetarian (conducts) */
        eat_corpse(mch);
    } else if (inv[s].otyp == O_CARROT) {
        item_inv_remove((uint8_t)s);
        nutrition += 50;                     /* a snack, not a meal */
        if (nutrition > 1500) nutrition = 1500;
        if (st_blind) {                      /* NetHack's carrot: the eyes clear */
            st_blind = 0;
            map_dirty = 1;                   /* redraw: the world comes back */
            msg("Your vision clears!");
        } else {
            msg("Crunchy.  Good for the eyes.");
        }
    } else {
        nutrition += 800;
        if (nutrition > 1500) nutrition = 1500;
        item_inv_remove((uint8_t)s);
        msg("You eat.  Delicious!");
    }
    sfx_eat();
    acted = 1; turns++;
}

/* The scroll of genocide: name a monster by its letter and the dungeon never
 * sends another -- those on this level die where they stand, and so does your
 * dog if you name dogs, as in NetHack. Cursed, it runs backwards: a pack of
 * whatever you named arrives around you. The shopkeeper and the Amulet's
 * guardian are beyond it; the mimic's two letters are one monster. */
static void genocide(uint8_t cursed)
{
    uint8_t tries, i;
    int k;
    char ch = 0;
    msg("Genocide what? (a letter)");
    for (tries = 0; tries < 3 && !ch; tries++) {
        in_wait_nokey();
        k = getkey();
        in_wait_nokey();
        if (k == MON_KEEPER || k == 'M') {
            msg("That one is beyond your power.");
            return;
        }
        if (k > ' ' && k < 127 && mon_find((char)k)->ch == (char)k)
            ch = (char)k;               /* mon_find falls back to the rat */
        else
            msg("No such creature.  Which letter?");
    }
    if (!ch) { msg("The scroll turns to dust."); return; }
    if (ch == 'x') ch = 'm';            /* the hidden mimic's letter */
    if (cursed) {
        if (summon_near(ch)) msg2("The ", mon_name(ch), " horde arrives!");
        else                 msg("You hear a distant rumble.");
        return;
    }
    mon_geno_set(ch);
    if (ch == 'm') mon_geno_set('x');
    for (i = 0; i < mcount; i++) {
        if (!m_alive[i]) continue;
        if (m_type[i] != ch && !(ch == 'm' && m_type[i] == 'x')) continue;
        m_alive[i] = 0;
        if ((int8_t)i == pet_idx) { pet_idx = -1; have_pet = 0; pet_hp = 0; }
    }
    map_flush = 1;                      /* +zx: they vanish across the map */
    msg2("Wiped out: ", mon_name(ch), ".");
}

/* The scroll of charging: blessed fills a wand to the brim, uncursed adds
 * three to five, cursed drains it dry. With no wand to charge it charges YOU
 * -- your power comes back, NetHack's confused-reading gift, so the scroll is
 * never a blank. Nine is the brim: the charges show as one digit. */
static void charging(uint8_t buc)
{
    int w = select_item('C');
    if (w == -1) { pw = pmaxpw; msg("You feel charged up!"); return; }
    if (w == -2) { msg("Never mind."); return; }
    if (buc == BUC_CURSE) { inv[w].ench = 0; msg("The wand is drained!"); return; }
    if (buc == BUC_BLESS) {
        inv[w].ench = 9;
    } else {
        inv[w].ench = (int8_t)(inv[w].ench + 3 + rn2(3));
        if (inv[w].ench > 9) inv[w].ench = 9;
    }
    msg("Your wand glows blue.");
}

void do_read(void) __banked
{
    int s = select_item('?');
    uint8_t ot, buc;

    if (s == -1) { msg("You have nothing to read."); return; }
    if (s == -2) { msg("Never mind."); return; }
    ot = inv[s].otyp;
    if (item_obj_cls(ot) == '&') {      /* a spellbook: study it (it survives) */
        cnt_reads++;                    /* studying breaks Illiterate too */
        learn_spell(item_obj_prop(ot));
        acted = 1; turns++;
        return;
    }
    cnt_reads++;                /* scrolls break Illiterate (conducts) */
    buc = (uint8_t)buc_st(&inv[s]);  /* before it leaves the pack */
    item_id_set(ot);                 /* reading it identifies the type */
    item_inv_remove((uint8_t)s);
    sfx_magic();
    if (ot == O_MAPPING) {
        fov_reveal();
        map_flush = 1;   /* +zx: seen-bits changed but vis didn't -- show the map */
        msg("The level map fills your mind!");
    } else if (ot == O_IDENTIFY) {
        /* NetHack's blessed identify, simplified for the Z80: the whole pack.
         * Every carried type is learned and every BUC state revealed. */
        uint8_t i;
        for (i = 0; i < inv_count; i++) {
            item_id_set(inv[i].otyp);
            inv[i].buc |= BUC_KNOWN;
        }
        msg("You feel knowledgeable!");
    } else if (ot == O_TELEPORT) {
        msg("You feel a wrenching sensation.");
        hero_teleport();                /* where a ring of control asks */
    } else if (ot == O_ENCHW || ot == O_ENCHA) {
        /* sharpen the wielded weapon / temper the worn armour (+1, derust) */
        char cls = (ot == O_ENCHW) ? ')' : '[';
        int i = item_pick_worn(cls);        /* any worn piece, not always the first */
        uint8_t hit = 0;
        if (i >= 0) {
            if (buc == BUC_CURSE) {         /* NetHack's cursed enchantment: -1 */
                if (inv[i].ench > -3) inv[i].ench--;
            } else {
                if (inv[i].ench < 5) inv[i].ench++;
                inv[i].ero = 0;
            }
            item_recompute_gear();
            hit = 1;
        }
        if (!hit)                msg(ot == O_ENCHW ? "Your hands itch." : "Your skin itches.");
        else if (buc == BUC_CURSE) msg(ot == O_ENCHW ? "Your weapon glows black!"
                                                     : "Your armor glows black!");
        else                     msg(ot == O_ENCHW ? "Your weapon glows blue!"
                                                     : "Your armor glows silver!");
    } else if (ot == O_SGENO) {
        genocide((uint8_t)(buc == BUC_CURSE));
    } else if (ot == O_SCHARGE) {
        charging(buc);
    } else if (ot == O_SDESTROY) {
        item_destroy_armor();
    } else if (ot == O_SAMNESIA) {
        fov_forget();                   /* this level's map ... */
        item_forget_ids();              /* ... and a third of the looks */
        map_dirty = 1;                  /* +zx: repaint the forgotten map */
        msg("Who was that Maud person anyway?");
    } else {                            /* O_RMCURSE */
        if (buc == BUC_CURSE) msg("You feel like you need help.");
        else msg(pray_uncurse(1) ? "Your burdens are lifted."
                                 : "Nothing happens.");
    }
    acted = 1; turns++;
}

/* read one movement key into a unit direction; 0 if it was not a direction */
static int read_dir(int *dx, int *dy)
{
    int k;
    in_wait_nokey();
    k = getkey();
    in_wait_nokey();
    *dx = 0; *dy = 0;
    switch (k) {
        case 'h': case  8: *dx = -1; break;
        case 'l': case  9: *dx = +1; break;
        case 'j': case 10: *dy = +1; break;
        case 'k': case 11: *dy = -1; break;
        case 'y': *dx = -1; *dy = -1; break;
        case 'u': *dx = +1; *dy = -1; break;
        case 'b': *dx = -1; *dy = +1; break;
        case 'n': *dx = +1; *dy = +1; break;
        default: return 0;
    }
    return 1;
}

/* Throw a carried weapon in a chosen direction. It flies in a straight line up
 * to THROW_RANGE cells, passing over the pet and the shopkeeper, until it
 * strikes the first enemy (damage by the weapon's power) or a wall. It then
 * lands on the floor where it came to rest and can be walked over and picked
 * back up (floor_drop), unless it stopped on rough terrain. Your wielded weapon
 * is thrown only after a confirmation, so you don't disarm yourself by mistake. */
#define THROW_RANGE 8
void do_throw(void) __banked
{
    int s = select_item(')');
    int dx, dy, x, y, r;
    uint8_t dmg, worn;
    obj_t thrown;

    if (s == -1) { msg("You have no weapon to throw."); return; }
    if (s == -2) { msg("Never mind."); return; }

    worn = inv[s].worn;
    if (worn) {                          /* don't disarm yourself by accident */
        int k;
        msg("Throw your wielded weapon?  y/n");
        in_wait_nokey();
        k = getkey();
        if (k != 'y' && k != 'Y') { msg("Never mind."); return; }
    }

    msg("In what direction?");
    if (!read_dir(&dx, &dy)) { msg("Never mind."); return; }

    thrown = inv[s];                     /* keep a copy: it lands on the floor */
    thrown.worn = 0;                     /* a weapon on the floor is not wielded */
    dmg = (uint8_t)(item_obj_prop(thrown.otyp)
                    + (thrown.ench > 0 ? thrown.ench : 0) + rn2(3));

    x = hero_x; y = hero_y;
    for (r = 0; r < THROW_RANGE; r++) {
        int nx = x + dx, ny = y + dy, mi;
        if (!walkable(terrain(nx, ny))) break;        /* a wall ahead: stop here */
        x = nx; y = ny;
        mi = monster_at(x, y);
        if (mi < 0) continue;
        if (mi == pet_idx || m_type[mi] == MON_KEEPER) continue;  /* fly past */
        hit_monster((uint8_t)mi, dmg);
        break;                                         /* lands at the enemy's feet */
    }
    item_inv_remove((uint8_t)s);
    if (worn) item_recompute_gear();          /* you just threw what you were wielding */
    item_floor_drop((uint8_t)x, (uint8_t)y, &thrown);       /* leave it to be reclaimed */
    sfx_hit();
    acted = 1; turns++;
}

/* Zap a wand. Digging bores straight down (you drop a level); the others fire a
 * bolt in a chosen direction that flies up to ZAP_RANGE cells, over the pet and
 * the shopkeeper, and acts on the monster(s) it meets: striking damages the
 * first, cold chills every monster in the line, sleep dozes the first, and
 * teleportation whisks the first away. Each zap spends a charge (obj_t.ench). */
#define ZAP_RANGE 9
void do_zap(void) __banked
{
    int s = select_item('/');
    int dx, dy, x, y, r, hit = 0;
    uint8_t ot;

    if (s == -1) { msg("You have no wand to zap."); return; }
    if (s == -2) { msg("Never mind."); return; }
    if (inv[s].ench <= 0) { msg("The wand has no charge."); return; }
    ot = inv[s].otyp;

    if (ot == O_WDIG) {                  /* digging needs no aim -- it goes down */
        acted = 1; turns++;
        /* Refuse only where there is no floor below: the win level, and the
         * mines' bottom. `dlvl >= DLVL_AMULET` looked equivalent and was not
         * -- the mines run dlvl 51..54, all of them >= 50, so digging was
         * refused throughout the whole branch. The charge is spent AFTER the
         * refusal now; it used to burn on a dig that never happened. */
        if (dlvl == DLVL_AMULET ||
            dlvl == (uint16_t)(MINES_BASE + MINES_DEPTH - 1)) {
            msg("The floor here resists digging."); return;
        }
        inv[s].ench--;
        item_id_set(O_WDIG);             /* a hole under you: no mystery left */
        msg("You dig a hole and drop through!");
        sfx_stairs();
        dlvl++;
        build_level();
        hero_x = up_x; hero_y = up_y;
        place_pet();
        return;
    }

    msg("In what direction?");
    if (!read_dir(&dx, &dy)) { msg("Never mind."); return; }
    inv[s].ench--;                       /* a real zap spends a charge */
    sfx_magic();

    x = hero_x; y = hero_y;
    for (r = 0; r < ZAP_RANGE; r++) {
        int mi;
        x += dx; y += dy;
        if (ot == O_WOPEN) {             /* opening seeks the first locked door */
            if (terrain(x, y) == '+' && door_unlock((uint8_t)x, (uint8_t)y)) {
                msg("Klick!  The door unlocks.");
                hit = 1;
                break;
            }
            if (!walkable(terrain(x, y))) break;
            continue;                    /* and passes every monster by */
        }
        if (!walkable(terrain(x, y))) break;             /* a wall stops the bolt */
        mi = monster_at(x, y);
        if (mi < 0) continue;
        if (mi == pet_idx || m_type[mi] == MON_KEEPER) continue;  /* spare dog/keeper */
        hit = 1;
        if (ot == O_WSTRIKE) { hit_monster((uint8_t)mi, (uint8_t)(rn2(8) + 3)); break; }
        if (ot == O_WCOLD)   { hit_monster((uint8_t)mi, (uint8_t)(rn2(6) + 2)); continue; }
        /* the two new rays pass through the whole line, as cold does */
        if (ot == O_WFIRE)   { hit_monster((uint8_t)mi, (uint8_t)(rn2(8) + 2)); continue; }
        if (ot == O_WMMISSILE) { hit_monster((uint8_t)mi, (uint8_t)(rn2(4) + 2)); continue; }
        if (ot == O_WSLEEP)  { m_sleep[mi] = (uint8_t)(rn2(10) + 8);
                               msg2("The ", mon_name(m_type[mi]), " falls asleep."); break; }
        /* O_WTELE: whisk the monster to a random spot, off your back. The
         * destination needs exactly the guards the nymph's blink already
         * uses (steal_item): plain floor, nobody standing there, not your
         * own cell -- and NEVER inside a shop. A monster left in a shop's
         * interior is frozen for good: both AI paths refuse a shop cell as
         * a destination, so once every neighbour is one it can never step
         * again, which turned the wand into a permanent off switch.
         * If no spot qualifies it simply stays put. */
        { uint8_t tx, ty, t;
          for (t = 0; t < 12; t++) {
              rand_floor((uint8_t)rn2(rcount), &tx, &ty);
              if (lvl[ty][tx] != '.') continue;
              if (monster_at((int)tx, (int)ty) >= 0) continue;
              if (shop_in_room((int)tx, (int)ty)) continue;
              if ((int)tx == hero_x && (int)ty == hero_y) continue;
              m_x[mi] = tx; m_y[mi] = ty;
              break;
          }
          msg2("The ", mon_name(m_type[mi]),
               (t < 12) ? " vanishes!" : " shudders."); }
        break;
    }
    /* A wand is learned by watching it work, as in NetHack: a bolt that met
     * something shows what it is; one that fizzled into an empty room keeps
     * its secret (and its look), so a first zap at nothing tells you nothing. */
    if (hit) item_id_set(ot);
    else     msg("The bolt fizzles out.");
    acted = 1; turns++;
}
