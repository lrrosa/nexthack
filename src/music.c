/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* music.c - the AY title theme.
 *
 * A deliberately small 3-voice sequencer rather than a tracker replayer: the
 * title needs one loop, and a few hundred bytes of player we fully understand
 * beats importing a PT3 engine. Voices are lead / bass / arpeggio, the classic
 * AY division of labour -- each channel plays one note at a time, so "chords"
 * are a single channel cycling root-third-fifth fast enough to be heard as one.
 *
 * The theme is in D Dorian: minor enough for a dungeon, but the raised sixth
 * (B natural) keeps it off the funeral march and turns the IV major, which is
 * the mode's signature colour. It answers the title art -- a proud knight, a
 * moonlit castle behind, the demon and the treasure ahead.
 *
 * BANKING: cold code, and on the 128K it joins platform_init in the spare
 * BANK_7. Note that the song tables are const-banked with it, so nothing
 * outside this file may read them. */

#include <z80.h>          /* z80_outp (newlib: _DEVELOPMENT/common/z80.h) */
#include <input.h>        /* in_inkey */
#include <stdint.h>
#include "music.h"

#ifdef __ZXNEXT
#pragma codeseg PAGE_20_CODE
#pragma constseg PAGE_20_CODE
#else
#pragma codeseg BANK_7
#pragma constseg BANK_7
#endif

/* ---- the chip ----
 * AY-3-8912: register select on 0xFFFD, data on 0xBFFD. Both ports have A15
 * high, so neither can be mistaken for the 0x7FFD paging latch (which needs
 * A15 low) -- true on a real 128K, on the Next, and on the RAM-expansion
 * interfaces we support. A machine with no AY simply swallows the writes and
 * the title plays in silence, which is the right way to degrade. */
#define AY_SEL 0xFFFDu
#define AY_DAT 0xBFFDu

static void ay(uint8_t reg, uint8_t val)
{
    z80_outp(AY_SEL, reg);
    z80_outp(AY_DAT, val);
}

/* ---- notes ----
 * AY tone period = clock / (16 * freq), clock = 1773400 Hz on both targets.
 * Four octaves stored exactly (96 B, nothing next to this bank's free space)
 * rather than one octave shifted down: shifting truncates, and this way every
 * note is as close as the chip allows. The residual error is the AY's own --
 * up to ~6 cents at the top, where a semitone is only a few period units
 * wide; the low octaves are within a cent. Periods are 12-bit and the
 * deepest here is 1695, well inside that. */
static const uint16_t periods[48] = {
    /*        C     C#    D     D#    E     F     F#    G     G#    A     A#    B  */
    /* 2 */ 1695, 1599, 1510, 1425, 1345, 1270, 1198, 1131, 1068, 1008,  951,  898,
    /* 3 */  847,  800,  755,  712,  673,  635,  599,  566,  534,  504,  476,  449,
    /* 4 */  424,  400,  377,  356,  336,  317,  300,  283,  267,  252,  238,  224,
    /* 5 */  212,  200,  189,  178,  168,  159,  150,  141,  133,  126,  119,  112
};

#define C_ 0
#define D_ 2
#define E_ 4
#define F_ 5
#define G_ 7
#define A_ 9
#define B_ 11
#define N(oct, semi) ((uint8_t)((oct) * 12 + (semi) + 1))   /* 0 stays "rest" */
#define REST 0

/* ---- timing ----
 * Durations are 50 Hz ticks: 28 per quarter note is ~107 BPM, walking pace --
 * enough momentum to feel like an adventure, slow enough to stay stately. */
#define Q   28
#define E_8 14
#define H   56
#define DH  84
#define W  112

typedef struct { uint8_t note, ticks; } ev_t;

/* ---- the song ----
 * A: the heroic arch (bars 1-8), rising to the octave and falling back
 *    through the Dorian B natural.
 * B: the darker answer (bars 9-16), leaning on the minor v and the flat side,
 *    then resolving home.
 * Each voice loops independently but the bar counts match, so they realign. */
static const ev_t lead[] = {
    { N(4,D_), H  }, { N(4,F_), Q }, { N(4,A_), Q },          /* 1 */
    { N(5,D_), DH }, { N(5,C_), Q },                          /* 2 */
    { N(4,B_), Q }, { N(4,A_), Q }, { N(4,G_), Q }, { N(4,F_), Q },  /* 3 */
    { N(4,E_), H }, { N(4,D_), H },                           /* 4 */
    { N(4,A_), Q }, { N(5,D_), Q }, { N(5,F_), H },           /* 5 */
    { N(5,E_), DH }, { N(5,D_), Q },                          /* 6 */
    { N(5,C_), Q }, { N(4,B_), Q }, { N(4,A_), Q }, { N(4,G_), Q },  /* 7 */
    { N(4,A_), W },                                           /* 8 */
    { N(4,F_), Q }, { N(4,A_), Q }, { N(5,C_), H },           /* 9 */
    { N(4,B_), Q }, { N(4,A_), Q }, { N(4,G_), H },           /* 10 */
    { N(4,E_), Q }, { N(4,G_), Q }, { N(4,B_), H },           /* 11 */
    { N(4,A_), W },                                           /* 12 */
    { N(5,D_), Q }, { N(5,C_), Q }, { N(4,B_), Q }, { N(4,A_), Q },  /* 13 */
    { N(4,G_), H }, { N(4,F_), H },                           /* 14 */
    { N(4,E_), Q }, { N(4,F_), Q }, { N(4,G_), Q }, { N(4,A_), Q },  /* 15 */
    { N(4,D_), W },                                           /* 16 */
    { REST, 0 }        /* 0 ticks = end marker: wrap to the top */
};

/* root on the downbeat, fifth at the half -- a walking bass without wandering */
static const ev_t bass[] = {
    { N(2,D_), H }, { N(2,A_), H },        /* 1  Dm */
    { N(2,D_), H }, { N(2,A_), H },        /* 2  Dm */
    { N(2,G_), H }, { N(3,D_), H },        /* 3  G  */
    { N(2,A_), H }, { N(3,E_), H },        /* 4  Am */
    { N(2,D_), H }, { N(2,A_), H },        /* 5  Dm */
    { N(2,A_), H }, { N(3,E_), H },        /* 6  Am */
    { N(2,F_), H }, { N(3,C_), H },        /* 7  F  */
    { N(2,A_), H }, { N(3,E_), H },        /* 8  Am */
    { N(2,F_), H }, { N(3,C_), H },        /* 9  F  */
    { N(2,G_), H }, { N(3,D_), H },        /* 10 G  */
    { N(2,E_), H }, { N(2,B_), H },        /* 11 Em */
    { N(2,A_), H }, { N(3,E_), H },        /* 12 Am */
    { N(2,D_), H }, { N(2,A_), H },        /* 13 Dm */
    { N(2,G_), H }, { N(3,D_), H },        /* 14 G  */
    { N(2,A_), H }, { N(3,E_), H },        /* 15 Am */
    { N(2,D_), H }, { N(2,A_), H },        /* 16 Dm */
    { REST, 0 }
};

/* One chord per bar. Bit 7 of the note marks a MAJOR triad -- in Dorian the
 * IV (G) is major, which is exactly the sound that keeps this from being a
 * plain minor tune. The player cycles root/third/fifth every tick. */
#define MAJ 0x80
static const ev_t arp[] = {
    { N(4,D_),       W }, { N(4,D_),       W },        /* Dm Dm */
    { N(4,G_) | MAJ, W }, { N(4,A_),       W },        /* G  Am */
    { N(4,D_),       W }, { N(4,A_),       W },        /* Dm Am */
    { N(4,F_) | MAJ, W }, { N(4,A_),       W },        /* F  Am */
    { N(4,F_) | MAJ, W }, { N(4,G_) | MAJ, W },        /* F  G  */
    { N(4,E_),       W }, { N(4,A_),       W },        /* Em Am */
    { N(4,D_),       W }, { N(4,G_) | MAJ, W },        /* Dm G  */
    { N(4,A_),       W }, { N(4,D_),       W },        /* Am Dm */
    { REST, 0 }
};

/* ---- per-voice playback state ---- */
typedef struct {
    const ev_t *song;
    uint8_t     idx;      /* current event                                  */
    uint8_t     left;     /* ticks remaining on it                          */
    uint8_t     note;     /* 0 = silent                                     */
    uint8_t     vol;      /* current volume, decayed for shape              */
    uint8_t     base;     /* volume a fresh note starts at                  */
    uint8_t     decay;    /* ticks per volume step down (0 = hold)          */
} voice_t;

#define NOTE_LO N(2, C_)                    /* the table's first entry */

static uint16_t note_period(uint8_t note)
{
    uint8_t i = (uint8_t)(note - NOTE_LO);
    return (i < 48) ? periods[i] : 0;        /* out of range -> silence */
}

static void voice_advance(voice_t *v)
{
    if (v->song[v->idx].ticks == 0) v->idx = 0;     /* end marker: loop      */
    v->note = v->song[v->idx].note;
    v->left = v->song[v->idx].ticks;
    v->vol  = v->base;
    v->idx++;
}

/* Play one 50 Hz tick of all three voices into the chip. */
static void tick(voice_t *lv, voice_t *bv, voice_t *av, uint8_t phase)
{
    uint16_t p;
    uint8_t  n;

    if (lv->left == 0) voice_advance(lv);
    if (bv->left == 0) voice_advance(bv);
    if (av->left == 0) voice_advance(av);

    /* lead + bass: one note each */
    p = lv->note ? note_period(lv->note) : 0;
    ay(0, (uint8_t)(p & 0xFF));  ay(1, (uint8_t)(p >> 8));
    p = bv->note ? note_period(bv->note) : 0;
    ay(2, (uint8_t)(p & 0xFF));  ay(3, (uint8_t)(p >> 8));

    /* arpeggio: step through the triad, a new tone every tick, so the ear
     * fuses them into a chord (one AY channel can only ever hold one note) */
    n = (uint8_t)(av->note & 0x7F);
    if (n) {
        uint8_t third = (uint8_t)((av->note & MAJ) ? 4 : 3);
        if (phase == 1) n = (uint8_t)(n + third);
        else if (phase == 2) n = (uint8_t)(n + 7);
        p = note_period(n);
    } else {
        p = 0;
    }
    ay(4, (uint8_t)(p & 0xFF));  ay(5, (uint8_t)(p >> 8));

    ay(7, 0x38);                  /* mixer: three tones on, no noise         */
    ay(8,  lv->note ? lv->vol : 0);
    ay(9,  bv->note ? bv->vol : 0);
    ay(10, av->note ? av->vol : 0);

    /* let each note breathe: a slow decay reads as plucked rather than organ */
    if (lv->decay && lv->vol && (lv->left % lv->decay) == 0) lv->vol--;
    if (av->decay && av->vol && (av->left % av->decay) == 0) av->vol--;
    lv->left--; bv->left--; av->left--;
}

void music_silence(void) __banked
{
    ay(8, 0); ay(9, 0); ay(10, 0);     /* all three volumes off */
}

/* Play the theme until a key is pressed, and return the entropy gathered
 * while waiting. The title screen has always seeded the world from how long
 * the player took to press -- keeping that is why the inner wait spins on
 * in_inkey() and stirs the counter every poll rather than sleeping: a 50 Hz
 * loop alone would leave only a few hundred distinguishable outcomes. */
uint16_t music_title_wait(void) __banked
{
    voice_t lv = { lead, 0, 0, 0, 0, 14, 9 };
    voice_t bv = { bass, 0, 0, 0, 0, 13, 0 };
    voice_t av = { arp,  0, 0, 0, 0,  7, 0 };
    uint16_t s = 1;
    uint8_t  phase = 0;

    for (;;) {
        tick(&lv, &bv, &av, phase);
        if (++phase > 2) phase = 0;

#ifdef __ZXNEXT
        {   /* the bare .nex runs with interrupts off, so FRAMES never ticks:
             * the poll loop IS the clock, calibrated like getkey_rpt's guard */
            uint16_t guard = 370;
            while (--guard) {
                if (in_inkey()) { music_silence(); return s; }
                s += 0x9E37u;
            }
        }
#else
        {   /* the 128K keeps the ROM's IM1 handler, so FRAMES is a real 50 Hz */
            volatile uint8_t *fr = (volatile uint8_t *)23672;
            uint8_t f = *fr;
            while (*fr == f) {
                if (in_inkey()) { music_silence(); return s; }
                s += 0x9E37u;
            }
        }
#endif
    }
}
