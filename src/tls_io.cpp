/*
   tls_io.cpp -- WolfSSL <-> mTCP I/O bridge
   Compiled as C++ (mTCP requires C++).
   Exposes a C-callable interface declared in tls_io.h.
*/

/* mTCP headers use FILE* and expect stdio.h to be included first */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* mTCP requires CFG_H to be defined on the compiler command line.
   It points to our per-application configuration header. */
#include "arp.h"
#include "dns.h"
#include "packet.h"
#include "tcp.h"
#include "tcpsockm.h"
#include "timer.h"
#include "utils.h"

/* WolfSSL headers are C -- wrap in extern "C" for C++ compilation */
extern "C" {
#include <wolfssl/ssl.h>
#include "tls_io.h"
#include "entropy.h"
}

/* -----------------------------------------------------------------------
 * Tuning constants
 */
#define NOS_TCP_RECV_BUF    4096   /* mTCP internal receive buffer per socket */
#define NOS_CONNECT_TIMEOUT 10000  /* ms to wait for TCP connect */
#define NOS_RECV_TIMEOUT    15000  /* ms to wait for data before giving up */
#define NOS_LOCAL_PORT_BASE 2048   /* random local port base */

/* -----------------------------------------------------------------------
 * nos_tls_init
 *
 * Initialise entropy (for WolfSSL RNG) and WolfSSL library.
 * mTCP stack init (Utils::parseEnv + Utils::initStack) is the caller's
 * responsibility because it must happen once at program startup and hooks
 * the timer interrupt -- it cannot be tucked inside a connect call.
 */
extern "C" int nos_tls_init( void ) {
    nos_entropy_init();
    if ( wolfSSL_Init() != WOLFSSL_SUCCESS ) return -1;
    return 0;
}

/* -----------------------------------------------------------------------
 * nos_dns_resolve -- resolve hostname to IpAddr_t using mTCP DNS.
 * Returns 0 on success, -1 on failure.
 */
static int nos_dns_resolve( const char *host, IpAddr_t &addr ) {
    int8_t rc;

    rc = Dns::resolve( (char *)host, addr, 1 );
    if ( rc < 0 ) {
        fprintf( stderr, "DNS: failed to start query for %s\n", host );
        return -1;
    }

    /* Poll until resolved or timed out */
    while ( Dns::isQueryPending() ) {
        PACKET_PROCESS_MULT(5);
        Arp::driveArp();
        Tcp::drivePackets();
        Dns::drivePendingQuery();
    }

    rc = Dns::resolve( (char *)host, addr, 0 );
    if ( rc != 0 ) {
        fprintf( stderr, "DNS: could not resolve %s\n", host );
        return -1;
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * nos_tls_connect -- DNS resolve + TCP connect + TLS handshake.
 */
extern "C" int nos_tls_connect( nos_tls_ctx_t *c, const char *host,
                                 unsigned int port ) {
    WOLFSSL_METHOD *method;
    TcpSocket      *sock;
    IpAddr_t        hostAddr;
    clockTicks_t    start;
    int             rc;

    memset( c, 0, sizeof(*c) );

    /* --- DNS --- */
    if ( nos_dns_resolve( host, hostAddr ) != 0 ) return -1;

    /* --- TCP connect --- */
    sock = TcpSocketMgr::getSocket();
    if ( sock == NULL ) {
        fprintf( stderr, "TLS: no free sockets\n" );
        return -1;
    }

    if ( sock->setRecvBuffer( NOS_TCP_RECV_BUF ) ) {
        fprintf( stderr, "TLS: failed to set recv buffer\n" );
        TcpSocketMgr::freeSocket( sock );
        return -1;
    }

    {
        uint16_t localPort = (uint16_t)(NOS_LOCAL_PORT_BASE + (rand() & 0x3FFF));
        if ( sock->connectNonBlocking( localPort, hostAddr, (uint16_t)port ) ) {
            fprintf( stderr, "TLS: TCP connect failed\n" );
            TcpSocketMgr::freeSocket( sock );
            return -1;
        }
    }

    /* Poll until connected or timed out */
    start = TIMER_GET_CURRENT();
    rc    = -1;
    while (1) {
        PACKET_PROCESS_MULT(5);
        Tcp::drivePackets();
        Arp::driveArp();

        if ( sock->isConnectComplete() ) { rc = 0; break; }

        if ( sock->isClosed() ||
             Timer_diff( start, TIMER_GET_CURRENT() ) >
                 TIMER_MS_TO_TICKS( NOS_CONNECT_TIMEOUT ) ) {
            break;
        }
    }

    if ( rc != 0 ) {
        fprintf( stderr, "TLS: TCP connect timed out\n" );
        sock->close();
        TcpSocketMgr::freeSocket( sock );
        return -1;
    }

    c->tcp_sock = (void *)sock;

    /* --- WolfSSL context --- */
    method = wolfTLSv1_2_client_method();
    if ( method == NULL ) goto fail_tcp;

    c->ctx = wolfSSL_CTX_new( method );
    if ( c->ctx == NULL ) goto fail_tcp;

    /* TOFU: we verify the cert fingerprint ourselves, not via CA chain */
    wolfSSL_CTX_set_verify( c->ctx, WOLFSSL_VERIFY_NONE, NULL );

    wolfSSL_CTX_SetIOSend( c->ctx, nos_tls_send );
    wolfSSL_CTX_SetIORecv( c->ctx, nos_tls_recv );

    c->ssl = wolfSSL_new( c->ctx );
    if ( c->ssl == NULL ) goto fail_ctx;

    wolfSSL_SetIOReadCtx(  c->ssl, c );
    wolfSSL_SetIOWriteCtx( c->ssl, c );

    /* SNI: tell the server which hostname we expect (required for virtual hosting) */
    wolfSSL_UseSNI( c->ssl, WOLFSSL_SNI_HOST_NAME,
                    host, (word16)strlen( host ) );

    /* TLS handshake */
    if ( wolfSSL_connect( c->ssl ) != WOLFSSL_SUCCESS ) {
        int                  err = wolfSSL_get_error( c->ssl, 0 );
        char                 estr[80];
        WOLFSSL_ALERT_HISTORY ah;
        wolfSSL_ERR_error_string_n( (unsigned long)err, estr, sizeof(estr) );
        fprintf( stderr, "TLS: handshake failed (err %d: %s)\n", err, estr );
        if ( wolfSSL_get_alert_history( c->ssl, &ah ) == WOLFSSL_SUCCESS ) {
            fprintf( stderr, "TLS: alert rx: level=%d code=%d\n",
                     ah.last_rx.level, ah.last_rx.code );
            fprintf( stderr, "TLS: alert tx: level=%d code=%d\n",
                     ah.last_tx.level, ah.last_tx.code );
        }
        goto fail_ssl;
    }

    return 0;

fail_ssl:
    wolfSSL_free( c->ssl );
    c->ssl = NULL;
fail_ctx:
    wolfSSL_CTX_free( c->ctx );
    c->ctx = NULL;
fail_tcp:
    sock->close();
    TcpSocketMgr::freeSocket( sock );
    c->tcp_sock = NULL;
    return -1;
}

/* -----------------------------------------------------------------------
 * nos_tls_close -- TLS shutdown + TCP close.
 */
extern "C" void nos_tls_close( nos_tls_ctx_t *c ) {
    TcpSocket *sock = (TcpSocket *)c->tcp_sock;

    if ( c->ssl ) {
        wolfSSL_shutdown( c->ssl );
        wolfSSL_free( c->ssl );
        c->ssl = NULL;
    }
    if ( c->ctx ) {
        wolfSSL_CTX_free( c->ctx );
        c->ctx = NULL;
    }
    if ( sock ) {
        sock->close();
        TcpSocketMgr::freeSocket( sock );
        c->tcp_sock = NULL;
    }
}

/* -----------------------------------------------------------------------
 * nos_tls_send -- WolfSSL send callback.
 *
 * mTCP's send() copies data into a transmit buffer and queues it.
 * We limit each call to getSuggestedSendSize() to respect the remote window
 * and our transmit buffer pool. WolfSSL will retry if we return fewer bytes
 * than requested.
 */
extern "C" int nos_tls_send( WOLFSSL *ssl, char *buf, int sz, void *ctx ) {
    nos_tls_ctx_t *c    = (nos_tls_ctx_t *)ctx;
    TcpSocket     *sock = (TcpSocket *)c->tcp_sock;
    uint16_t       canSend;
    int16_t        sent;

    (void)ssl;

    if ( sock->isClosed() ) return WOLFSSL_CBIO_ERR_CONN_CLOSE;

    canSend = sock->getSuggestedSendSize();
    if ( canSend == 0 ) {
        PACKET_PROCESS_MULT(5);
        Tcp::drivePackets();
        return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }

    if ( (uint16_t)sz > canSend ) sz = (int)canSend;

    sent = sock->send( (uint8_t *)buf, (uint16_t)sz );
    if ( sent == (int16_t)sz ) {
        PACKET_PROCESS_MULT(5);
        Tcp::drivePackets();
        return (int)sent;
    }
    if ( sent == TCP_RC_NO_XMIT_BUFFERS ) return WOLFSSL_CBIO_ERR_WANT_WRITE;
    return WOLFSSL_CBIO_ERR_GENERAL;
}

/* -----------------------------------------------------------------------
 * nos_tls_recv -- WolfSSL receive callback.
 *
 * Poll mTCP until data is available or the connection closes.
 * WolfSSL retries automatically when we return WOLFSSL_CBIO_ERR_WANT_READ.
 */
extern "C" int nos_tls_recv( WOLFSSL *ssl, char *buf, int sz, void *ctx ) {
    nos_tls_ctx_t *c    = (nos_tls_ctx_t *)ctx;
    TcpSocket     *sock = (TcpSocket *)c->tcp_sock;
    clockTicks_t   start;
    int16_t        got;

    (void)ssl;

    /* Try once before starting the timeout loop */
    got = sock->recv( (uint8_t *)buf, (uint16_t)sz );
    if ( got > 0 ) return (int)got;
    if ( sock->isRemoteClosed() ) return WOLFSSL_CBIO_ERR_CONN_CLOSE;

    /* Poll until data arrives or timeout */
    start = TIMER_GET_CURRENT();
    while (1) {
        PACKET_PROCESS_MULT(5);
        Tcp::drivePackets();

        got = sock->recv( (uint8_t *)buf, (uint16_t)sz );
        if ( got > 0 ) return (int)got;
        if ( sock->isRemoteClosed() ) return WOLFSSL_CBIO_ERR_CONN_CLOSE;

        if ( Timer_diff( start, TIMER_GET_CURRENT() ) >
                 TIMER_MS_TO_TICKS( NOS_RECV_TIMEOUT ) ) {
            break;
        }
    }

    return WOLFSSL_CBIO_ERR_WANT_READ;
}
