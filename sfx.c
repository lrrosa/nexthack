/* sfx.c - beeper sound effects using z88dk's bit_beepfx library.
 *
 * The beeper effects are cycle-timed for a 3.5 MHz Spectrum, but the game
 * runs at 28 MHz, which would make them play 8x too fast/high. So each effect
 * temporarily drops the CPU to 3.5 MHz and restores 28 MHz afterwards. Each
 * effect is short, so the brief slowdown is unnoticeable in this turn-based
 * game. Interrupts are disabled during playback (the _di variant) for a clean
 * tone; the hardware tilemap and polled keyboard don't need them.
 */

#include "sfx.h"
#include <sound.h>
#include <arch/zxn.h>

static void play(void *fx)
{
    ZXN_WRITE_REG(0x07, 0x00);   /* 3.5 MHz so the beeper is in tune */
    bit_beepfx_di(fx);
    ZXN_WRITE_REG(0x07, 0x03);   /* restore 28 MHz turbo             */
}

void sfx_hit(void)     { play((void *)BEEPFX_HIT_1);   }
void sfx_kill(void)    { play((void *)BEEPFX_BOOM_2);  }
void sfx_hurt(void)    { play((void *)BEEPFX_HIT_4);   }
void sfx_pick(void)    { play((void *)BEEPFX_PICK);    }
void sfx_gold(void)    { play((void *)BEEPFX_SCORE);   }
void sfx_quaff(void)   { play((void *)BEEPFX_GULP);    }
void sfx_eat(void)     { play((void *)BEEPFX_EAT);     }
void sfx_stairs(void)  { play((void *)BEEPFX_SWITCH_1);}
void sfx_levelup(void) { play((void *)BEEPFX_YEAH);    }
void sfx_die(void)     { play((void *)BEEPFX_AWW);     }
