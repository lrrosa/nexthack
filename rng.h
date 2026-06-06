/* rng.h - small xorshift16 PRNG */
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

extern uint16_t world_seed;     /* fixed per game; levels derive from it */

void     rng_set(uint16_t s);   /* set the generator state directly      */
void     rng_seed(void);        /* seed world_seed from machine state    */
uint16_t rng_next(void);
uint8_t  rn2(uint8_t n);        /* uniform 0 .. n-1                      */

#endif /* RNG_H */
