/* entropy.h -- DOS entropy source for WolfSSL custom RNG
 *
 * Wired into WolfSSL via CUSTOM_RAND_GENERATE = nos_rand_byte.
 * Call nos_entropy_init() once at startup before any TLS operation.
 */

#ifndef NOS_ENTROPY_H
#define NOS_ENTROPY_H

/*
 * nos_entropy_init -- seed the RNG from available DOS entropy sources.
 * Must be called once before nos_rand_byte() is used.
 */
void nos_entropy_init(void);

/*
 * nos_rand_byte -- return one pseudo-random byte.
 * This is the hook called by WolfSSL (CUSTOM_RAND_GENERATE).
 */
unsigned char nos_rand_byte(void);

#endif /* NOS_ENTROPY_H */
