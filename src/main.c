/* main.c -- Entry point and UI loop (Phase 6) (C89)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <conio.h>

#include "arp.h"
#include "dns.h"
#include "packet.h"
#include "tcp.h"
#include "tcpsockm.h"
#include "timer.h"
#include "utils.h"

#include "url.h"
#include "gemini.h"
#include "render.h"
#include "nav.h"
#include "ui.h"
#include <wolfssl/ssl.h>

#define DEFAULT_URL "gemini://geminiprotocol.net/"

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
    unsigned int raw_lines;
} nos_body_state_t;

static void raw_line_break( nos_body_state_t *st ) {
    if ( st == NULL ) return;
    st->raw_lines++;
    if ( st->raw_lines >= 24 ) {
        fputs("--More--", stdout);
        (void)getch();
        fputs("\r        \r", stdout);
        st->raw_lines = 0;
    }
}

static int body_cb( const char *chunk, unsigned int len, void *user ) {
    nos_body_state_t *st = (nos_body_state_t *)user;
    unsigned int i;
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
        for ( i = 0; i < len; i++ ) {
            if ( chunk[i] == '\n' ) raw_line_break( st );
        }
    }
    return 0;
}

static int fetch_and_render( const nos_url_t *url, nos_render_ctx_t *out_ctx ) {
    nos_gemini_resp_t resp;
    nos_gemini_err_t err;
    unsigned int bodyLen = 0;
    nos_body_state_t bodyState;

    if ( out_ctx ) {
        nos_render_ctx_init( out_ctx );
    }

    bodyState.resp = &resp;
    bodyState.decided = 0;
    bodyState.is_gem = 0;
    bodyState.raw_lines = 0;

    if ( nos_gemini_request_stream( url, &resp, body_cb, &bodyState, &bodyLen, &err ) != 0 ) {
        fprintf( stderr, "Gemini request failed: %s\n", err.msg[0] ? err.msg : "unknown" );
        return -1;
    }

    if ( resp.status >= 20 && resp.status < 30 ) {
        if ( !bodyState.decided ) {
            printf( "--- Response ---\n" );
        }
        if ( bodyState.is_gem ) {
            if ( nos_render_stream_flush( &bodyState.rstream ) != 0 ) {
                fprintf( stderr, "Render failed\n" );
            }
            if ( out_ctx ) *out_ctx = bodyState.rctx;
        }
        if ( userAbort ) printf( "\n(Aborted)\n" );
        printf( "\n--- End ---\n" );
        return 0;
    }

    printf( "Status: %d\n", resp.status );
    printf( "Meta: %s\n", resp.meta );
    return 0;
}

static int parse_user_url( const char *input, nos_url_t *out ) {
    char temp[600];
    if ( input == NULL || out == NULL ) return -1;
    if ( strncmp( input, "gemini://", 9 ) == 0 ) {
        return nos_url_parse( input, out );
    }
    if ( strstr( input, "://" ) != NULL ) return -1;
    if ( strlen( input ) + 9 >= sizeof( temp ) ) return -1;
    strcpy( temp, "gemini://" );
    strcat( temp, input );
    return nos_url_parse( temp, out );
}

static int is_number( const char *s ) {
    if ( s == NULL || *s == '\0' ) return 0;
    while ( *s ) {
        if ( *s < '0' || *s > '9' ) return 0;
        s++;
    }
    return 1;
}

int main( int argc, char *argv[] ) {
    nos_url_t current;
    nos_url_t next;
    nos_nav_t history;
    nos_render_ctx_t links;
    char input[256];

    (void)argc;
    (void)argv;

    if ( Utils::parseEnv() != 0 ) {
        fprintf( stderr, "Failed to read mTCP config (MTCPCFG not set?)\n" );
        return 1;
    }

    if ( Utils::initStack( 1, TCP_SOCKET_RING_SIZE, ctrlBreakHandler, ctrlBreakHandler ) ) {
        fprintf( stderr, "Failed to initialise TCP/IP stack\n" );
        return 1;
    }

    if ( nos_url_parse( DEFAULT_URL, &current ) != 0 ) {
        fprintf( stderr, "Invalid default URL\n" );
        shutdown_stack( 1 );
    }

    if ( nos_tls_init() != 0 ) {
        fprintf( stderr, "Failed to initialise WolfSSL\n" );
        shutdown_stack( 1 );
    }

    nos_nav_init( &history );

    while ( 1 ) {
        printf( "\n== %s:%u%s ==\n", current.host, current.port, current.path );
        if ( fetch_and_render( &current, &links ) != 0 ) {
            fprintf( stderr, "Fetch failed\n" );
        }

        if ( nos_ui_prompt( "Command (number/url/b/r/q): ", input, sizeof( input ) ) != 0 ) break;
        if ( input[0] == '\0' ) continue;

        if ( strcmp( input, "q" ) == 0 ) break;
        if ( strcmp( input, "r" ) == 0 ) continue;
        if ( strcmp( input, "b" ) == 0 ) {
            if ( nos_nav_pop( &history, &next ) == 0 ) {
                current = next;
            } else {
                printf( "No history\n" );
            }
            continue;
        }

        if ( is_number( input ) ) {
            unsigned int idx = (unsigned int)atoi( input );
            if ( idx == 0 || idx > links.link_count ) {
                printf( "Invalid link number\n" );
                continue;
            }
            if ( nos_url_resolve( &current, links.links[idx - 1].url, &next ) != 0 ) {
                printf( "Failed to resolve link\n" );
                continue;
            }
            nos_nav_push( &history, &current );
            current = next;
            continue;
        }

        if ( parse_user_url( input, &next ) != 0 ) {
            printf( "Invalid URL\n" );
            continue;
        }
        nos_nav_push( &history, &current );
        current = next;
    }

    if ( getenv( "NOS_SKIP_WOLFSSL_CLEANUP" ) == NULL ) {
        wolfSSL_Cleanup();
    }
    shutdown_stack( 0 );
    return 0;
}
