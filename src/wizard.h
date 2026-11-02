/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* wizard.h - the Wizard of Yendor (2.0), wizard.c's interface.
 *
 * wizard.c is RESIDENT on the 128K and BANKED on the Next (banks.json): the
 * 128K's banks are all within a few hundred bytes of full while its resident
 * half has room, and the Next is the other way about. WIZ_ENTRY is what its
 * entry points are on each, so every caller -- one call each, in the fuller
 * banks -- is written once for both. */
#ifndef WIZARD_H
#define WIZARD_H

#include <stdint.h>

#ifdef __ZXNEXT
#define WIZ_ENTRY __banked
#else
#define WIZ_ENTRY
#endif

#define WIZ_CH 'Y'      /* his montypes[] row (monster.c) */

/* What the Wizard is doing, carried across levels and saved whole (SAVE_VER
 * 42). His monster slot is not: a level's slots are rebuilt on every visit,
 * so wiz_tick puts him back beside you wherever you are. */
enum { WZ_NONE,         /* the Amulet has not left Dlvl 50 yet */
       WZ_UP,           /* abroad: on your level, or arriving on it */
       WZ_GONE };       /* counting down to his (re)appearance */
typedef struct {
    uint8_t  st;        /* WZ_* */
    uint8_t  hp;        /* his hit points, from level to level */
    uint8_t  amu;       /* 1: he holds the Amulet (stolen from you) */
    uint8_t  buc;       /* ...and its BUC byte, to give it back as it was */
    uint8_t  mend;      /* 1: away because he was hurt, not killed */
    uint8_t  deaths;    /* how often you killed him: his greeting knows */
    uint16_t timer;     /* turns until he comes, while WZ_GONE */
} wiz_t;
extern wiz_t wiz;

void wiz_tick(void) WIZ_ENTRY;           /* upkeep: once a turn */
void wiz_steal(uint8_t i) WIZ_ENTRY;     /* his bite landed (ATK_AMU) */
void wiz_died(void) WIZ_ENTRY;           /* drop_held: the Wizard is dead */

#endif
