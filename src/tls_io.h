/* tls_io.h -- WolfSSL <-> mTCP bridge (C-callable interface)
 *
 * tls_io.cpp is compiled as C++; this header is safe to include from C.
 * WolfSSL types are used in nos_tls_ctx_t so include wolfssl/ssl.h first.
 */

#ifndef NOS_TLS_IO_H
#define NOS_TLS_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wolfssl/ssl.h>

typedef struct {
    WOLFSSL_CTX *ctx;
    WOLFSSL     *ssl;
    void        *tcp_sock;   /* TcpSocket* -- opaque to C callers */
} nos_tls_ctx_t;

/*
 * nos_tls_init -- one-time WolfSSL + mTCP stack initialisation.
 * Call once at program startup (after Utils::parseEnv()).
 * Returns 0 on success, -1 on failure.
 */
int nos_tls_init(void);

/*
 * nos_tls_connect -- DNS resolve, TCP connect, TLS handshake.
 * Returns 0 on success, -1 on failure.
 */
int nos_tls_connect(nos_tls_ctx_t *c, const char *host, unsigned int port);

/*
 * nos_tls_close -- TLS shutdown + TCP close + free resources.
 */
void nos_tls_close(nos_tls_ctx_t *c);

/*
 * nos_tls_send / nos_tls_recv -- WolfSSL I/O callbacks.
 * Registered internally; do not call directly.
 */
int nos_tls_send(WOLFSSL *ssl, char *buf, int sz, void *ctx);
int nos_tls_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NOS_TLS_IO_H */
