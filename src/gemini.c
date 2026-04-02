/* gemini.c -- Gemini protocol request/response (C89)
 */

#include <stdio.h>
#include <string.h>
#include "gemini.h"
#include "tls_io.h"
#include "tofu.h"

#define NOS_GEMINI_MAX_HEADER 1024
#define NOS_GEMINI_MAX_REDIRECTS 5

static void set_err(nos_gemini_err_t *err, int code, int tls_err, const char *msg)
{
    if (err == NULL) return;
    err->code = code;
    err->tls_err = tls_err;
    err->redirect_limit = 0;
    if (msg) {
        strncpy(err->msg, msg, sizeof(err->msg) - 1);
        err->msg[sizeof(err->msg) - 1] = '\0';
    } else {
        err->msg[0] = '\0';
    }
}

static int read_header_line(WOLFSSL *ssl, char *buf, unsigned int maxLen)
{
    unsigned int n = 0;
    int got;
    char c;
    char prev = '\0';

    if (maxLen == 0) return -1;

    while (n + 1 < maxLen) {
        got = wolfSSL_read(ssl, &c, 1);
        if (got <= 0) return -1;
        buf[n++] = c;
        buf[n] = '\0';
        if (prev == '\r' && c == '\n') {
            return 0;
        }
        prev = c;
    }

    return -1;
}

static int parse_header(const char *line, int *status, char *meta, unsigned int metaMax)
{
    unsigned int i = 0;
    int code = 0;

    if (line == NULL || status == NULL || meta == NULL) return -1;
    if (metaMax == 0) return -1;

    while (line[i] == ' ' || line[i] == '\t') i++;

    if (line[i] < '0' || line[i] > '9') return -1;
    code = (line[i] - '0') * 10;
    i++;
    if (line[i] < '0' || line[i] > '9') return -1;
    code += (line[i] - '0');
    i++;

    *status = code;

    while (line[i] == ' ' || line[i] == '\t') i++;

    {
        unsigned int m = 0;
        while (line[i] != '\0' && !(line[i] == '\r' && line[i+1] == '\n')) {
            if (m + 1 >= metaMax) return -1;
            meta[m++] = line[i++];
        }
        meta[m] = '\0';
    }

    return 0;
}

static int is_unreserved(char c)
{
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '-' || c == '.' || c == '_' || c == '~') return 1;
    return 0;
}

static int url_encode_query(const char *in, char *out, unsigned int outMax)
{
    unsigned int n = 0;
    unsigned int i = 0;
    char hex[] = "0123456789ABCDEF";

    if (outMax == 0) return -1;

    while (in[i] != '\0') {
        unsigned char c = (unsigned char)in[i++];
        if (is_unreserved((char)c)) {
            if (n + 1 >= outMax) return -1;
            out[n++] = (char)c;
        } else {
            if (n + 3 >= outMax) return -1;
            out[n++] = '%';
            out[n++] = hex[(c >> 4) & 0x0F];
            out[n++] = hex[c & 0x0F];
        }
    }
    out[n] = '\0';
    return 0;
}

static int apply_query(const nos_url_t *base, const char *query, nos_url_t *out)
{
    char encoded[512];
    char newPath[512];
    const char *qmark;
    unsigned int baseLen;

    if (base == NULL || query == NULL || out == NULL) return -1;

    if (url_encode_query(query, encoded, sizeof(encoded)) != 0) return -1;

    qmark = strchr(base->path, '?');
    if (qmark) {
        baseLen = (unsigned int)(qmark - base->path);
    } else {
        baseLen = (unsigned int)strlen(base->path);
    }

    if (baseLen + 1 + strlen(encoded) >= sizeof(newPath)) return -1;
    memcpy(newPath, base->path, baseLen);
    newPath[baseLen] = '\0';
    strcat(newPath, "?");
    strcat(newPath, encoded);

    *out = *base;
    strcpy(out->path, newPath);
    return 0;
}

typedef struct {
    char *buf;
    unsigned int max;
    unsigned int total;
} nos_buf_writer_t;

static int buffer_writer_cb(const char *chunk, unsigned int len, void *user)
{
    nos_buf_writer_t *w = (nos_buf_writer_t *)user;
    if (w == NULL || w->buf == NULL) return -1;
    if (w->total + len > w->max) return -1;
    memcpy(w->buf + w->total, chunk, len);
    w->total += len;
    return 0;
}

int nos_gemini_request(const nos_url_t *url, nos_gemini_resp_t *resp,
                       char *body_buf, unsigned int body_max,
                       unsigned int *body_len,
                       nos_gemini_err_t *err)
{
    nos_buf_writer_t writer;
    if (body_buf == NULL || body_max == 0) {
        set_err(err, -1, 0, "invalid body buffer");
        return -1;
    }
    writer.buf = body_buf;
    writer.max = body_max;
    writer.total = 0;

    if (body_len) *body_len = 0;

    if (nos_gemini_request_stream(url, resp, buffer_writer_cb, &writer, body_len, err) != 0) {
        return -1;
    }
    if (body_len) *body_len = writer.total;
    return 0;
}

int nos_gemini_request_stream(const nos_url_t *url, nos_gemini_resp_t *resp,
                              nos_gemini_body_cb cb, void *user,
                              unsigned int *body_len,
                              nos_gemini_err_t *err)
{
    nos_url_t current;
    nos_url_t next;
    int redirects = 0;
    int rc;
    nos_tls_ctx_t tls;
    char header[NOS_GEMINI_MAX_HEADER];
    char request[768];
    unsigned int total;
    int bytes;
    char chunk[512];

    if (err) {
        err->code = 0;
        err->tls_err = 0;
        err->redirect_limit = 0;
        err->msg[0] = '\0';
    }

    if (url == NULL || resp == NULL) {
        set_err(err, -1, 0, "invalid arguments");
        return -1;
    }
    if (cb == NULL) {
        set_err(err, -1, 0, "missing body callback");
        return -1;
    }

    current = *url;
    resp->status = 0;
    resp->meta[0] = '\0';
    if (body_len) *body_len = 0;

    while (1) {
        rc = nos_tls_connect(&tls, current.host, current.port);
        if (rc != 0) {
            set_err(err, -1, rc, "TLS connect failed");
            return -1;
        }

        {
            char terr[128];
            terr[0] = '\0';
            if (nos_tofu_check(tls.ssl, current.host, current.port, terr, sizeof(terr)) != 0) {
                nos_tls_close(&tls);
                set_err(err, -1, 0, terr[0] ? terr : "untrusted certificate");
                return -1;
            }
        }

        if (strlen(current.host) + strlen(current.path) + 12 >= sizeof(request)) {
            nos_tls_close(&tls);
            set_err(err, -1, 0, "request too long");
            return -1;
        }
        sprintf(request, "gemini://%s%s\r\n", current.host, current.path);

        bytes = wolfSSL_write(tls.ssl, request, (int)strlen(request));
        if (bytes <= 0) {
            nos_tls_close(&tls);
            set_err(err, -1, 0, "TLS write failed");
            return -1;
        }

        if (read_header_line(tls.ssl, header, sizeof(header)) != 0) {
            nos_tls_close(&tls);
            set_err(err, -1, 0, "header read failed");
            return -1;
        }

        if (parse_header(header, &resp->status, resp->meta, sizeof(resp->meta)) != 0) {
            nos_tls_close(&tls);
            set_err(err, -1, 0, "header parse failed");
            return -1;
        }

        if (resp->status >= 20 && resp->status < 30) {
            total = 0;
            while (1) {
                bytes = wolfSSL_read(tls.ssl, chunk, (int)sizeof(chunk));
                if (bytes <= 0) break;
                if (cb(chunk, (unsigned int)bytes, user) != 0) {
                    nos_tls_close(&tls);
                    set_err(err, -1, 0, "body callback aborted");
                    return -1;
                }
                total += (unsigned int)bytes;
            }
            if (body_len) *body_len = total;
            nos_tls_close(&tls);
            return 0;
        }

        if (resp->status >= 10 && resp->status < 20) {
            char line[256];
            printf("%s ", resp->meta[0] ? resp->meta : "Input:");
            if (fgets(line, sizeof(line), stdin) == NULL) {
                nos_tls_close(&tls);
                set_err(err, -1, 0, "input aborted");
                return -1;
            }
            {
                size_t len = strlen(line);
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                    line[len-1] = '\0';
                    len--;
                }
            }
            nos_tls_close(&tls);
            if (apply_query(&current, line, &next) != 0) {
                set_err(err, -1, 0, "query encoding failed");
                return -1;
            }
            current = next;
            continue;
        }

        if (resp->status >= 30 && resp->status < 40) {
            if (redirects++ >= NOS_GEMINI_MAX_REDIRECTS) {
                nos_tls_close(&tls);
                if (err) err->redirect_limit = 1;
                set_err(err, -1, 0, "redirect limit exceeded");
                return -1;
            }
            nos_tls_close(&tls);
            if (nos_url_resolve(&current, resp->meta, &next) != 0) {
                set_err(err, -1, 0, "redirect resolve failed");
                return -1;
            }
            current = next;
            continue;
        }

        if (resp->status >= 40 && resp->status < 60) {
            nos_tls_close(&tls);
            if (body_len) *body_len = 0;
            return 0;
        }

        if (resp->status >= 60 && resp->status < 70) {
            nos_tls_close(&tls);
            if (body_len) *body_len = 0;
            return 0;
        }

        nos_tls_close(&tls);
        return 0;
    }
}
