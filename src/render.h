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
} nos_render_ctx_t;

/*
 * nos_render_gemtext -- render gemtext body to stdout and collect links.
 * Returns 0 on success, -1 on error.
 */
int nos_render_gemtext(const char *body, unsigned int len, nos_render_ctx_t *ctx);

#endif /* NOS_RENDER_H */
