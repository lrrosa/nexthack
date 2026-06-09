/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* rng.c - small xorshift16 PRNG, seeded from the machine state */

#include "rng.h"
#include <arch/zxn.h>

static uint16_t rng = 1;
uint16_t world_seed = 1;

void rng_set(uint16_t s)
{
    rng = s ? s : 0xACE1u;
}

void rng_seed(void)
{
    uint16_t s = (uint16_t)ZXN_READ_REG(0x1F);       /* raster line low  */
    s = (uint16_t)((s << 8) ^ (uint16_t)ZXN_READ_REG(0x1E));
    s ^= *(volatile uint16_t *)0x5C78u;              /* FRAMES sysvar    */
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
