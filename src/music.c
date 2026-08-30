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
 * BANKING: cold code, with the song tables const-banked alongside it (see
 * banks.json), so nothing outside this file may read them. */

#include <z80.h>          /* z80_outp (newlib: _DEVELOPMENT/common/z80.h) */
#include <input.h>        /* in_inkey */
#include <stdint.h>
#include "music.h"

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

#define C_  0
#define Cs_ 1
#define D_  2
#define E_  4
#define F_  5
#define Fs_ 6
#define G_  7
#define A_  9
#define B_  11
#define N(oct, semi) ((uint8_t)((oct) * 12 + (semi) + 1))   /* 0 stays "rest" */
#define REST 0

/* ---- timing ----
 * Durations are 50 Hz ticks: 18 per quarter is ~167 BPM. Everything below is
 * expressed in Q, so changing it retimes the whole piece; keep Q even so the
 * quaver stays a whole number of ticks. */
#define Q   18
#define E_8 (Q / 2)
#define DQ  (Q + Q / 2)
#define H   (Q * 2)
#define DH  (Q * 3)
#define W   (Q * 4)
#define BAR (Q * 4)

typedef struct { uint8_t note, ticks; } ev_t;

/* ---- the song ----
 * D MAJOR, not the minor mode this started in: the overworld-fanfare sound
 * the game wanted is unambiguously major, and that is the single biggest
 * lever. On top of it the heroic devices -- the rising fourth and fifth in
 * bar 1 (a bugle can only play the harmonic series, which is why fanfares
 * are built from those leaps), dotted long-short figures for a martial gait,
 * and melody that walks the triad rather than the scale.
 * A: bars 1-8, the arch climbing to the high G and settling home.
 * B: bars 9-16, a turn through the relative minor for contrast, then back.
 * Each voice loops independently, but the bar counts match, so they realign. */
static const ev_t lead[] = {
    { N(3,A_), E_8 }, { N(4,D_), E_8 }, { N(4,Fs_), Q }, { N(4,A_), H }, /* 1 */
    { N(5,D_),  DH }, { N(4,A_), Q },                          /* 2  the octave */
    { N(4,B_),  Q }, { N(4,A_), Q }, { N(4,G_), H },           /* 3  */
    { N(4,Fs_), W },                                           /* 4  land       */
    { N(4,G_), E_8 }, { N(4,A_), E_8 }, { N(4,B_), Q }, { N(5,D_), H }, /* 5 */
    { N(5,C_),  DH }, { N(4,A_), Q },                          /* 6  the flat 7 */
    { N(4,B_),  Q }, { N(4,G_), Q }, { N(4,A_), H },           /* 7  */
    { N(4,D_),  W },                                           /* 8  home       */
    { N(5,D_),  Q }, { N(5,E_), Q }, { N(5,Fs_), H },          /* 9  */
    { N(5,E_),  DQ }, { N(5,D_), E_8 }, { N(4,B_), H },        /* 10 */
    { N(5,C_),  Q }, { N(5,D_), Q }, { N(5,E_), H },           /* 11 */
    { N(5,D_),  W },                                           /* 12 */
    { N(4,A_), E_8 }, { N(4,B_), E_8 }, { N(5,C_), Q }, { N(5,D_), H }, /* 13 */
    { N(5,E_),  DQ }, { N(5,Fs_), E_8 }, { N(5,G_), H },       /* 14 the peak   */
    { N(5,Fs_), Q }, { N(5,E_), Q }, { N(5,D_), Q }, { N(5,C_), Q },    /* 15 */
    { N(5,D_),  W },      /* 16 the flat 7 steps UP to the tonic: dropping a
                           * seventh to D4 here read as a stumble, not a
                           * landing (bar 8 still falls to D4, but only a
                           * fifth, off a held A4 -- that one lands)        */
    { REST, 0 }        /* 0 ticks = end marker: wrap to the top */
};

/* Root and fifth alternating in QUARTERS, not halves: at 150 BPM the bass is
 * the engine, and four beats to the bar push where two only sat. */
static const ev_t bass[] = {
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 1  D  */
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 2  D  */
    { N(2,G_), Q }, { N(3,D_), Q }, { N(2,G_), Q }, { N(3,D_), Q },   /* 3  G  */
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 4  D  */
    { N(2,G_), Q }, { N(3,D_), Q }, { N(2,G_), Q }, { N(3,D_), Q },   /* 5  G  */
    { N(3,C_), Q }, { N(3,G_), Q }, { N(3,C_), Q }, { N(3,G_), Q },   /* 6  C  */
    { N(2,G_), Q }, { N(3,D_), Q }, { N(2,G_), Q }, { N(3,D_), Q },   /* 7  G  */
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 8  D  */
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 9  D  */
    { N(2,B_), Q }, { N(3,Fs_), Q }, { N(2,B_), Q }, { N(3,Fs_), Q }, /* 10 Bm */
    { N(3,C_), Q }, { N(3,G_), Q }, { N(3,C_), Q }, { N(3,G_), Q },   /* 11 C  */
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 12 D  */
    { N(2,G_), Q }, { N(3,D_), Q }, { N(2,G_), Q }, { N(3,D_), Q },   /* 13 G  */
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 14 D  */
    { N(3,C_), Q }, { N(3,G_), Q }, { N(3,C_), Q }, { N(3,G_), Q },   /* 15 C  */
    { N(2,D_), Q }, { N(2,A_), Q }, { N(2,D_), Q }, { N(2,A_), Q },   /* 16 D  */
    { REST, 0 }
};

/* One chord per bar; bit 7 marks a MAJOR triad. The player cycles
 * root/third/fifth every tick, which one AY channel hears as a chord. */
#define MAJ 0x80
static const ev_t arp[] = {
    { N(3,D_) | MAJ, W }, { N(3,D_) | MAJ, W },        /* D  D  */
    { N(3,G_) | MAJ, W }, { N(3,D_) | MAJ, W },        /* G  D  */
    { N(3,G_) | MAJ, W }, { N(3,C_) | MAJ, W },        /* G  C  <- the flat 7 */
    { N(3,G_) | MAJ, W }, { N(3,D_) | MAJ, W },        /* G  D  */
    { N(3,D_) | MAJ, W }, { N(3,B_),       W },        /* D  Bm */
    { N(3,C_) | MAJ, W }, { N(3,D_) | MAJ, W },        /* C  D  */
    { N(3,G_) | MAJ, W }, { N(3,D_) | MAJ, W },        /* G  D  */
    { N(3,C_) | MAJ, W }, { N(3,D_) | MAJ, W },        /* C  D  */
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

/* ---- the drum ----
 * The AY has its own envelope generator, and it makes a far better percussive
 * decay than stepping the volume every tick in software: shape 0 is \___,
 * full to silence and hold, and rewriting R13 RETRIGGERS it. A channel opts
 * in by setting bit 4 of its volume register (0x10) instead of a level.
 * The generator is shared by all three channels, but only the drum uses it --
 * lead and bass hold fixed levels -- so there is nothing to collide with.
 *
 * Envelope period is 16 steps of 256 clocks: time = 256 * period / 1773400.
 * KICK ~87 ms, SNARE ~50 ms, which is thump versus crack.
 *
 * Kick lands on beats 1 and 3, snare on 2 and 4. That backbeat is the point:
 * the first attempt hit ticks 0 and 40 of an 80-tick bar, which is beats 1
 * and 3 -- doubling the bass instead of answering it, and plodding for it. */
#define ENV_SHAPE_DECAY 0
#define KICK_ENV  600
#define SNARE_ENV 350
#define KICK_TONE 1510        /* D2: a low thump, not a pitch you follow */
#define DRUM_LEN  7           /* ticks channel C is on drum duty          */

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

/* Play one 50 Hz tick of all three voices into the chip. `beat` counts ticks
 * inside the bar, and drives the drum: a short noise burst on beats 2 and 4,
 * layered over the arpeggio channel (the AY lets a channel carry tone AND
 * noise at once). It is the backbeat, and it is what turns a chord loop into
 * something that marches. */
static void tick(voice_t *lv, voice_t *bv, voice_t *av, uint8_t phase, uint8_t beat)
{
    uint16_t p;
    uint8_t  n, hit;

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

    /* Which drum, and how far into it? Kick on beats 1 and 3, snare on 2
     * and 4; channel C lends itself to the drum for DRUM_LEN ticks and goes
     * back to the arpeggio after. */
    hit = 0;
    {
        uint8_t into = (uint8_t)(beat % Q);          /* ticks into this beat */
        if (into < DRUM_LEN)
            hit = (uint8_t)(((beat / Q) & 1) ? 2 : 1);   /* even beat = kick */
        if (hit && into == 0) {                      /* the attack tick      */
            uint16_t ep = (hit == 1) ? KICK_ENV : SNARE_ENV;
            ay(11, (uint8_t)(ep & 0xFF));
            ay(12, (uint8_t)(ep >> 8));
            ay(13, ENV_SHAPE_DECAY);   /* writing R13 retriggers the decay   */
        }
    }

    if (hit == 1) {                    /* kick: a low TONE on C, no noise    */
        ay(4, (uint8_t)(KICK_TONE & 0xFF));
        ay(5, (uint8_t)(KICK_TONE >> 8));
        ay(7, 0x38);
    } else if (hit == 2) {             /* snare: NOISE on C, its tone muted  */
        ay(6, 17);                     /* noise period: low and dry          */
        ay(7, 0x1C);
    } else {
        ay(7, 0x38);
    }

    ay(8,  lv->note ? lv->vol : 0);
    ay(9,  bv->note ? bv->vol : 0);
    /* 0x10 = "use the envelope generator" instead of a fixed level */
    ay(10, hit ? 0x10 : (av->note ? av->vol : 0));

    /* let each note breathe: a slow decay reads as plucked rather than organ */
    if (lv->decay && lv->vol > 9 && (lv->left % lv->decay) == 0) lv->vol--;
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
    /* Volumes near the chip's ceiling of 15. The AY's steps are roughly
     * logarithmic, so the audible lift comes less from the last step or two
     * than from keeping them there: the lead's decay now floors at 9 instead
     * of fading a long note to nothing. */
    voice_t lv = { lead, 0, 0, 0, 0, 15, 12 };
    voice_t bv = { bass, 0, 0, 0, 0, 15, 0 };
    voice_t av = { arp,  0, 0, 0, 0,  9, 0 };   /* support, not sparkle */
    uint16_t s = 1;
    uint8_t  phase = 0, beat = 0;

    for (;;) {
        tick(&lv, &bv, &av, phase, beat);
        if (++phase > 2) phase = 0;
        if (++beat >= BAR) beat = 0;

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
