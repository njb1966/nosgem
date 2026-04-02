/* gemini.h -- Gemini protocol request/response (C89)
 */

#ifndef NOS_GEMINI_H
#define NOS_GEMINI_H

#include "url.h"

typedef struct {
    int  status;
    char meta[1024];
} nos_gemini_resp_t;

typedef struct {
    int code;              /* 0 on success, -1 on failure */
    int tls_err;           /* TLS/connect error if code != 0 */
    int redirect_limit;    /* set when redirect limit exceeded */
    char msg[128];         /* short diagnostic */
} nos_gemini_err_t;

typedef int (*nos_gemini_body_cb)(const char *chunk, unsigned int len, void *user);

/*
 * nos_gemini_request -- perform a Gemini request.
 * Returns 0 on success (including non-2x Gemini responses), -1 on internal error.
 */
int nos_gemini_request(const nos_url_t *url, nos_gemini_resp_t *resp,
                       char *body_buf, unsigned int body_max,
                       unsigned int *body_len,
                       nos_gemini_err_t *err);

/*
 * nos_gemini_request_stream -- perform a Gemini request and stream body.
 * The callback is invoked for each data chunk (status 20 only).
 * Return non-zero from callback to abort.
 */
int nos_gemini_request_stream(const nos_url_t *url, nos_gemini_resp_t *resp,
                              nos_gemini_body_cb cb, void *user,
                              unsigned int *body_len,
                              nos_gemini_err_t *err);

#endif /* NOS_GEMINI_H */
