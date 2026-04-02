/* tofu.h -- TOFU certificate trust store (C89)
 */

#ifndef NOS_TOFU_H
#define NOS_TOFU_H

#include <wolfssl/ssl.h>

/*
 * nos_tofu_check -- compute peer fingerprint and apply TOFU policy.
 * Returns 0 on trust, -1 on failure or untrusted cert.
 */
int nos_tofu_check(WOLFSSL *ssl, const char *host, unsigned int port,
                   char *err, unsigned int errMax);

#endif /* NOS_TOFU_H */
