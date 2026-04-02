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
#include "tls_io.h"
}

#include "url.h"
#include "gemini.h"
#include "render.h"

#define DEFAULT_HOST "geminiprotocol.net"
#define DEFAULT_PORT 1965

static volatile int userAbort = 0;

static void __interrupt __far ctrlBreakHandler( void ) { userAbort = 1; }

static void shutdown_stack( int rc ) {
    Utils::endStack();
    exit( rc );
}

static int is_gemtext( const char *meta ) {
    if ( meta == NULL ) return 0;
    return ( strncmp( meta, "text/gemini", 11 ) == 0 );
}

int main( int argc, char *argv[] ) {
    const char   *host = DEFAULT_HOST;
    unsigned int  port = DEFAULT_PORT;
    const char   *rawUrl = NULL;
    nos_url_t     url;
    nos_gemini_resp_t resp;
    nos_gemini_err_t  err;
    char          body[8192];
    unsigned int  bodyLen;

    if ( argc >= 2 ) rawUrl = argv[1];
    if ( argc >= 3 ) port = (unsigned int)atoi( argv[2] );

    printf( "NOSgem TLS test\n" );
    if ( rawUrl && strncmp( rawUrl, "gemini://", 9 ) == 0 ) {
        if ( nos_url_parse( rawUrl, &url ) != 0 ) {
            fprintf( stderr, "Invalid URL: %s\n", rawUrl );
            return 1;
        }
        host = url.host;
        port = url.port;
    }

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

    /* Gemini request */
    if ( rawUrl && strncmp( rawUrl, "gemini://", 9 ) == 0 ) {
        /* url already parsed */
    } else {
        url.host[0] = '\0';
        url.path[0] = '\0';
        url.port = port;
        if ( strlen( host ) >= sizeof( url.host ) ) {
            fprintf( stderr, "Host name too long\n" );
            shutdown_stack( 1 );
        }
        strcpy( url.host, host );
        strcpy( url.path, "/" );
    }

    printf( "Requesting: gemini://%s:%u%s\n\n", url.host, url.port, url.path );

    if ( nos_gemini_request( &url, &resp, body, sizeof( body ), &bodyLen, &err ) != 0 ) {
        fprintf( stderr, "Gemini request failed: %s\n", err.msg[0] ? err.msg : "unknown" );
        shutdown_stack( 1 );
    }

    if ( resp.status >= 20 && resp.status < 30 ) {
        if ( bodyLen >= sizeof( body ) ) bodyLen = sizeof( body ) - 1;
        body[bodyLen] = '\0';
        if ( is_gemtext( resp.meta ) ) {
            nos_render_ctx_t rctx;
            printf( "--- Gemtext ---\n" );
            if ( nos_render_gemtext( body, bodyLen, &rctx ) != 0 ) {
                fprintf( stderr, "Render failed\n" );
            }
            printf( "\n--- End ---\n" );
        } else {
            printf( "--- Response ---\n" );
            printf( "%s", body );
            if ( userAbort ) printf( "\n(Aborted)\n" );
            printf( "\n--- End ---\n" );
        }
    } else {
        printf( "Status: %d\n", resp.status );
        printf( "Meta: %s\n", resp.meta );
    }

    wolfSSL_Cleanup();
    shutdown_stack( 0 );
    return 0;
}
