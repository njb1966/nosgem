/*
   tlstest.cpp -- Phase 2 integration test
   Note: stdio.h must be included before mTCP headers (they use FILE*).
   Connects to a Gemini server, completes TLS handshake,
   sends a Gemini request, and prints the raw response.

   Usage: TLSTEST.EXE [host] [port]
   Default: geminiprotocol.net 1965
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "arp.h"
#include "dns.h"
#include "packet.h"
#include "tcp.h"
#include "tcpsockm.h"
#include "timer.h"
#include "utils.h"

extern "C" {
#include <wolfssl/ssl.h>
#include "tls_io.h"
#include "entropy.h"
}

#define DEFAULT_HOST "geminiprotocol.net"
#define DEFAULT_PORT 1965
#define RESP_BUF_LEN 512

static volatile int userAbort = 0;

static void __interrupt __far ctrlBreakHandler( void ) { userAbort = 1; }

static void shutdown_stack( int rc ) {
    Utils::endStack();
    exit( rc );
}

int main( int argc, char *argv[] ) {
    const char   *host = DEFAULT_HOST;
    unsigned int  port = DEFAULT_PORT;
    nos_tls_ctx_t tls;
    char          request[256];
    char          respBuf[RESP_BUF_LEN];
    int           bytes;
    int           reqLen;

    if ( argc >= 2 ) host = argv[1];
    if ( argc >= 3 ) port = (unsigned int)atoi( argv[2] );

    printf( "NOSgem TLS test\n" );
    printf( "Target: %s:%u\n\n", host, port );

    /* mTCP stack init -- must happen before any TCP/DNS calls */
    if ( Utils::parseEnv() != 0 ) {
        fprintf( stderr, "Failed to read mTCP config (MTCPCFG not set?)\n" );
        return 1;
    }

    if ( Utils::initStack( 1, TCP_SOCKET_RING_SIZE,
                           ctrlBreakHandler, ctrlBreakHandler ) ) {
        fprintf( stderr, "Failed to initialise TCP/IP stack\n" );
        return 1;
    }

    /* WolfSSL + entropy init */
    if ( nos_tls_init() != 0 ) {
        fprintf( stderr, "Failed to initialise WolfSSL\n" );
        shutdown_stack( 1 );
    }

    /* Connect */
    printf( "Connecting...\n" );
    if ( nos_tls_connect( &tls, host, port ) != 0 ) {
        fprintf( stderr, "Connection failed\n" );
        shutdown_stack( 1 );
    }
    printf( "TLS handshake complete!\n" );
    printf( "Cipher: %s\n\n", wolfSSL_get_cipher_name( tls.ssl ) );

    /* Send Gemini request: "gemini://host/\r\n" */
    reqLen = sprintf( request, "gemini://%s/\r\n", host );
    if ( wolfSSL_write( tls.ssl, request, reqLen ) != reqLen ) {
        fprintf( stderr, "Send failed\n" );
        nos_tls_close( &tls );
        shutdown_stack( 1 );
    }

    /* Read and print response */
    printf( "--- Response ---\n" );
    while (1) {
        bytes = wolfSSL_read( tls.ssl, respBuf, RESP_BUF_LEN - 1 );
        if ( bytes <= 0 ) break;
        respBuf[bytes] = '\0';
        printf( "%s", respBuf );
        if ( userAbort ) break;
    }
    printf( "\n--- End ---\n" );

    nos_tls_close( &tls );
    wolfSSL_Cleanup();
    shutdown_stack( 0 );
    return 0;
}
