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

typedef struct {
    const nos_gemini_resp_t *resp;
    int decided;
    int is_gem;
    nos_render_ctx_t rctx;
    nos_render_stream_t rstream;
} nos_body_state_t;

static int body_cb( const char *chunk, unsigned int len, void *user ) {
    nos_body_state_t *st = (nos_body_state_t *)user;
    if ( st == NULL || st->resp == NULL ) return -1;

    if ( !st->decided ) {
        st->decided = 1;
        st->is_gem = is_gemtext( st->resp->meta );
        if ( st->is_gem ) {
            printf( "--- Gemtext ---\n" );
            nos_render_ctx_init( &st->rctx );
            st->rctx.page_lines = 24;
            nos_render_stream_init( &st->rstream, &st->rctx );
        } else {
            printf( "--- Response ---\n" );
        }
    }

    if ( st->is_gem ) {
        if ( nos_render_stream_feed( &st->rstream, chunk, len ) != 0 ) return -1;
    } else {
        if ( fwrite( chunk, 1, len, stdout ) != len ) return -1;
    }
    return 0;
}

int main( int argc, char *argv[] ) {
    const char   *host = DEFAULT_HOST;
    unsigned int  port = DEFAULT_PORT;
    const char   *rawUrl = NULL;
    nos_url_t     url;
    nos_gemini_resp_t resp;
    nos_gemini_err_t  err;
    unsigned int  bodyLen;
    nos_body_state_t bodyState;

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

    bodyLen = 0;
    bodyState.resp = &resp;
    bodyState.decided = 0;
    bodyState.is_gem = 0;

    if ( nos_gemini_request_stream( &url, &resp, body_cb, &bodyState, &bodyLen, &err ) != 0 ) {
        fprintf( stderr, "Gemini request failed: %s\n", err.msg[0] ? err.msg : "unknown" );
        shutdown_stack( 1 );
    }

    if ( resp.status >= 20 && resp.status < 30 ) {
        if ( !bodyState.decided ) {
            printf( "--- Response ---\n" );
        }
        if ( bodyState.is_gem ) {
            if ( nos_render_stream_flush( &bodyState.rstream ) != 0 ) {
                fprintf( stderr, "Render failed\n" );
            }
        }
        if ( userAbort ) printf( "\n(Aborted)\n" );
        printf( "\n--- End ---\n" );
    } else {
        printf( "Status: %d\n", resp.status );
        printf( "Meta: %s\n", resp.meta );
    }

    if ( getenv( "NOS_SKIP_WOLFSSL_CLEANUP" ) == NULL ) {
        wolfSSL_Cleanup();
    }
    shutdown_stack( 0 );
    return 0;
}
