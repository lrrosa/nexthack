/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* sfx.c - beeper sound effects using z88dk's bit_beepfx library.
 *
 * The beeper effects are cycle-timed for a 3.5 MHz Spectrum. On the Next the
 * game runs at 28 MHz, so each effect temporarily drops the CPU to 3.5 MHz and
 * restores 28 MHz afterwards; on the plain 128K (+zx) the CPU is already
 * 3.5 MHz, so no switching is needed. Interrupts are disabled during playback
 * (the _di variant) for a clean tone; the polled keyboard doesn't need them.
 */

#include "sfx.h"
#include <sound.h>
#ifdef __ZXNEXT
#include <arch/zxn.h>
#endif

/* Banked module (cold, event-driven). play() is static -> reached by in-page
 * calls from the sfx_* wrappers; bit_beepfx_di and the BEEPFX_* tables are
 * resident, so the banked code calls/reads them directly. */
#ifdef __ZXNEXT
#pragma codeseg PAGE_20_CODE
#else
#pragma codeseg BANK_1
#endif

static void play(void *fx)
{
#ifdef __ZXNEXT
    ZXN_WRITE_REG(0x07, 0x00);   /* 3.5 MHz so the beeper is in tune */
    bit_beepfx_di(fx);
    ZXN_WRITE_REG(0x07, 0x03);   /* restore 28 MHz turbo             */
#else
    bit_beepfx_di(fx);           /* +zx is already at 3.5 MHz */
#endif
}

void sfx_hit(void)     __banked { play((void *)BEEPFX_HIT_1);   }
void sfx_kill(void)    __banked { play((void *)BEEPFX_BOOM_2);  }
void sfx_hurt(void)    __banked { play((void *)BEEPFX_HIT_4);   }
void sfx_pick(void)    __banked { play((void *)BEEPFX_PICK);    }
void sfx_gold(void)    __banked { play((void *)BEEPFX_SCORE);   }
void sfx_quaff(void)   __banked { play((void *)BEEPFX_GULP);    }
void sfx_eat(void)     __banked { play((void *)BEEPFX_EAT);     }
void sfx_magic(void)   __banked { play((void *)BEEPFX_ROBOBLIP);}
void sfx_stairs(void)  __banked { play((void *)BEEPFX_SWITCH_1);}
void sfx_levelup(void) __banked { play((void *)BEEPFX_YEAH);    }
void sfx_die(void)     __banked { play((void *)BEEPFX_AWW);     }
