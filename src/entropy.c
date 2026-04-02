/* entropy.c -- DOS entropy source for WolfSSL custom RNG
 *
 * Strategy:
 *   Seed a 32-bit LCG with entropy gathered from three DOS sources:
 *     1. BIOS tick counter at 0040:006C  (18.2 Hz, changes each call)
 *     2. 8254 PIT channel 0 counter      (1.193 MHz, changes constantly)
 *     3. time() from the C runtime       (seconds since epoch)
 *
 *   nos_rand_byte() runs the LCG and also XORs in a fresh PIT read on
 *   every call, so consecutive bytes are not trivially predictable.
 *
 * Limitations:
 *   This is adequate for a TOFU-based Gemini client where the threat model
 *   is passive eavesdropping, not active key compromise. Do not use for
 *   anything requiring cryptographic-grade randomness.
 */

#include <conio.h>   /* inp(), outp() port I/O */
#include <time.h>
#include "entropy.h"

/* LCG state -- 32-bit, using unsigned long which is 32-bit in 16-bit mode */
static unsigned long nos_lcg_state = 0UL;

/* -----------------------------------------------------------------------
 * read_bios_ticks -- read the 32-bit BIOS tick counter at 0040:006C.
 * Returns the low 16 bits (sufficient for seeding).
 */
static unsigned int read_bios_ticks(void)
{
    unsigned int far *ticks;
    ticks = (unsigned int far *)0x0040006CUL;
    return *ticks;
}

/* -----------------------------------------------------------------------
 * read_pit -- read the current value of 8254 PIT channel 0.
 * Latch the counter first (OUT 0x43, 0x00), then read two bytes from 0x40.
 */
static unsigned int read_pit(void)
{
    unsigned char lo, hi;

    /* latch channel 0 */
    outp(0x43, 0x00);
    lo = inp(0x40);
    hi = inp(0x40);

    return ((unsigned int)hi << 8) | lo;
}

/* -----------------------------------------------------------------------
 * nos_entropy_init -- gather initial seed and initialise the LCG state.
 */
void nos_entropy_init(void)
{
    unsigned long seed;
    unsigned int  ticks1, ticks2, pit1, pit2;
    time_t        t;

    /* first set of readings */
    ticks1 = read_bios_ticks();
    pit1   = read_pit();

    /* small busy-wait to let the PIT advance */
    {
        volatile unsigned int i;
        for (i = 0; i < 1000U; i++) { ; }
    }

    /* second set of readings */
    ticks2 = read_bios_ticks();
    pit2   = read_pit();
    t      = time(NULL);

    /* mix all sources into a 32-bit seed */
    seed  = (unsigned long)ticks1;
    seed ^= (unsigned long)pit1   << 16;
    seed ^= (unsigned long)ticks2 <<  8;
    seed ^= (unsigned long)pit2;
    seed ^= (unsigned long)(unsigned int)t;
    seed ^= (unsigned long)(unsigned int)t << 13;

    /* never allow a zero state (LCG would get stuck) */
    if (seed == 0UL) seed = 0xDEADBEEFUL;

    nos_lcg_state = seed;
}

/* -----------------------------------------------------------------------
 * nos_rand_byte -- return one pseudo-random byte.
 * Called by WolfSSL via CUSTOM_RAND_GENERATE hook.
 *
 * LCG parameters from Knuth / Numerical Recipes (32-bit variant):
 *   state = state * 1664525 + 1013904223
 * XOR with a live PIT read on every call for additional mixing.
 */
unsigned char nos_rand_byte(void)
{
    nos_lcg_state = nos_lcg_state * 1664525UL + 1013904223UL;
    nos_lcg_state ^= (unsigned long)read_pit();
    return (unsigned char)(nos_lcg_state >> 24);
}
