/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* rng.c - small xorshift16 PRNG, seeded from the machine state */

#include "rng.h"
#ifdef __ZXNEXT
#include <arch/zxn.h>
#endif

static uint16_t rng = 1;
uint16_t world_seed = 1;

void rng_set(uint16_t s)
{
    rng = s ? s : 0xACE1u;
}

void rng_seed(void)
{
#ifdef __ZXNEXT
    uint16_t s = (uint16_t)ZXN_READ_REG(0x1F);       /* raster line low  */
    s = (uint16_t)((s << 8) ^ (uint16_t)ZXN_READ_REG(0x1E));
    s ^= *(volatile uint16_t *)0x5C78u;              /* FRAMES sysvar    */
#else
    /* +zx has no NextReg raster counter; seed from the FRAMES sysvar (a free-
     * running 50 Hz counter at 0x5C78). The dominant entropy comes from the
     * title screen's key-press timing anyway (see title_screen). */
    uint16_t s = *(volatile uint16_t *)0x5C78u;       /* FRAMES low word  */
    s ^= (uint16_t)((uint16_t)*(volatile uint8_t *)0x5C7Au << 8); /* FRAMES high byte */
#endif
    world_seed = s ? s : 0xACE1u;
    rng = world_seed;
}

uint16_t rng_next(void)
{
    rng ^= (uint16_t)(rng << 7);
    rng ^= (uint16_t)(rng >> 9);
    rng ^= (uint16_t)(rng << 8);
    return rng;
}

uint8_t rn2(uint8_t n)
{
    return n ? (uint8_t)(rng_next() % n) : 0;
}
