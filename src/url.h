/* url.h -- Gemini URL parsing and resolution (C89)
 */

#ifndef NOS_URL_H
#define NOS_URL_H

typedef struct {
    char host[256];
    unsigned int port;
    char path[512];
} nos_url_t;

/*
 * nos_url_parse -- parse a gemini:// URL into host/port/path.
 * Returns 0 on success, -1 on error.
 */
int nos_url_parse(const char *raw, nos_url_t *out);

/*
 * nos_url_resolve -- resolve link against base URL (for relative links).
 * Returns 0 on success, -1 on error.
 */
int nos_url_resolve(const nos_url_t *base, const char *link, nos_url_t *out);

#endif /* NOS_URL_H */
