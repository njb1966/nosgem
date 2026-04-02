/* render.h -- Gemtext renderer for DOS text mode (C89)
 */

#ifndef NOS_RENDER_H
#define NOS_RENDER_H

#define NOS_MAX_LINKS 256

typedef struct {
    char url[512];
    char label[256];
} nos_link_t;

typedef struct {
    nos_link_t links[NOS_MAX_LINKS];
    unsigned int link_count;
    unsigned int lines_printed;
    unsigned int page_lines;
    int (*pause_fn)(void *user);
    void *pause_user;
} nos_render_ctx_t;

typedef struct {
    nos_render_ctx_t *ctx;
    int in_pre;
    char line[1024];
    unsigned int line_len;
} nos_render_stream_t;

/*
 * nos_render_gemtext -- render gemtext body to stdout and collect links.
 * Returns 0 on success, -1 on error.
 */
int nos_render_gemtext(const char *body, unsigned int len, nos_render_ctx_t *ctx);

/*
 * Streaming renderer helpers
 */
void nos_render_ctx_init(nos_render_ctx_t *ctx);
void nos_render_stream_init(nos_render_stream_t *st, nos_render_ctx_t *ctx);
int nos_render_stream_feed(nos_render_stream_t *st, const char *data, unsigned int len);
int nos_render_stream_flush(nos_render_stream_t *st);

#endif /* NOS_RENDER_H */
