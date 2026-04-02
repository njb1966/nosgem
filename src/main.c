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
#include "config.h"
#include "bookmarks.h"
#include <wolfssl/ssl.h>

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
    unsigned int page_lines;
} nos_body_state_t;

static void raw_line_break( nos_body_state_t *st ) {
    if ( st == NULL ) return;
    st->raw_lines++;
    if ( st->page_lines > 0 && st->raw_lines >= st->page_lines ) {
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
            st->rctx.page_lines = st->page_lines;
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

static int fetch_and_render( const nos_url_t *url, nos_render_ctx_t *out_ctx, unsigned int page_lines ) {
    nos_gemini_resp_t resp;
    nos_gemini_err_t err;
    unsigned int bodyLen = 0;
    nos_body_state_t bodyState;

    if ( page_lines == 0 ) page_lines = 24;

    if ( out_ctx ) {
        nos_render_ctx_init( out_ctx );
    }

    bodyState.resp = &resp;
    bodyState.decided = 0;
    bodyState.is_gem = 0;
    bodyState.raw_lines = 0;
    bodyState.page_lines = page_lines;

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

static int render_homepage( const char *path, nos_render_ctx_t *links, unsigned int page_lines ) {
    FILE *homeFile;
    char buf[512];
    unsigned int read;
    nos_render_stream_t st;

    if ( links == NULL ) return -1;

    homeFile = fopen( path, "r" );
    if ( homeFile == NULL ) return -1;

    nos_render_ctx_init( links );
    links->page_lines = page_lines;
    nos_render_stream_init( &st, links );

    while ( (read = (unsigned int)fread( buf, 1, sizeof( buf ), homeFile )) > 0 ) {
        if ( nos_render_stream_feed( &st, buf, read ) != 0 ) break;
    }
    (void)nos_render_stream_flush( &st );
    fclose( homeFile );
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

static void make_current_url( const nos_url_t *cur, char *out, unsigned int outMax ) {
    if ( out == NULL || outMax == 0 || cur == NULL ) return;
    out[0] = '\0';
    if ( strlen( "gemini://" ) + strlen( cur->host ) + strlen( cur->path ) + 16 >= outMax ) return;
    strcat( out, "gemini://" );
    strcat( out, cur->host );
    if ( cur->port != 1965 ) {
        char portbuf[16];
        sprintf( portbuf, ":%u", cur->port );
        strcat( out, portbuf );
    }
    strcat( out, cur->path );
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
    nos_config_t cfg;
    FILE *homeFile;

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

    nos_config_defaults( &cfg );
    (void)nos_config_load( &cfg );

    if ( nos_url_parse( cfg.home, &current ) != 0 ) {
        fprintf( stderr, "Invalid HOME in config\n" );
        shutdown_stack( 1 );
    }

    if ( nos_tls_init() != 0 ) {
        fprintf( stderr, "Failed to initialise WolfSSL\n" );
        shutdown_stack( 1 );
    }

    nos_nav_init( &history );

    while ( 1 ) {
    if ( strcmp( current.host, "local" ) == 0 && strcmp( current.path, "/homepage" ) == 0 ) {
        printf( "\n== HOME ==\n" );
        if ( render_homepage( NOS_HOMEPAGE_PATH, &links, cfg.page_lines ) != 0 ) {
            printf( "Failed to open HOMEPAGE.GEM\n" );
        }
    } else if ( strcmp( current.host, "local" ) == 0 && strcmp( current.path, "/bookmarks" ) == 0 ) {
        printf( "\n== BOOKMARKS ==\n" );
        if ( render_homepage( NOS_BOOKMARKS_PATH, &links, cfg.page_lines ) != 0 ) {
            printf( "Failed to open BOOKMARKS.GEM\n" );
        }
    } else {
        printf( "\n== %s:%u%s ==\n", current.host, current.port, current.path );
        if ( fetch_and_render( &current, &links, cfg.page_lines ) != 0 ) {
            fprintf( stderr, "Fetch failed\n" );
        }
        }

        if ( nos_ui_prompt( "Command (number/url/b/r/h/B/a/q): ", input, sizeof( input ) ) != 0 ) break;
    if ( input[0] == '\0' ) continue;

    if ( strcmp( input, "q" ) == 0 ) break;
    if ( strcmp( input, "r" ) == 0 ) continue;
    if ( strcmp( input, "h" ) == 0 ) {
        nos_nav_push( &history, &current );
        strcpy( current.host, "local" );
        current.port = 0;
        strcpy( current.path, "/homepage" );
        continue;
    }
    if ( strcmp( input, "B" ) == 0 ) {
        nos_nav_push( &history, &current );
        strcpy( current.host, "local" );
        current.port = 0;
        strcpy( current.path, "/bookmarks" );
        continue;
    }
    if ( strcmp( input, "a" ) == 0 ) {
        char label[128];
        char urlbuf[600];
        make_current_url( &current, urlbuf, sizeof( urlbuf ) );
        if ( urlbuf[0] == '\0' ) {
            printf( "URL too long\n" );
            continue;
        }
        if ( nos_ui_prompt( "Label (optional): ", label, sizeof( label ) ) != 0 ) {
            printf( "Cancelled\n" );
            continue;
        }
        if ( nos_bookmarks_add( urlbuf, label ) != 0 ) {
            printf( "Failed to write bookmarks\n" );
        } else {
            printf( "Bookmarked\n" );
        }
        continue;
    }
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
