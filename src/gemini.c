/* gemini.c -- Gemini protocol request/response (C89)
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "gemini.h"
#include "tls_io.h"
#include <wolfssl/wolfcrypt/sha256.h>

#define NOS_GEMINI_MAX_HEADER 1024
#define NOS_GEMINI_MAX_REDIRECTS 5
#define NOS_KNOWN_HOSTS_PATH "C:\\known_hosts.txt"
#define NOS_KNOWN_HOSTS_TMP  "C:\\known_hosts.tmp"

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

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void rstrip_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = '\0';
        n--;
    }
}

static int compute_peer_fingerprint(WOLFSSL *ssl, char *out, int outLen)
{
    WOLFSSL_X509 *cert;
    const unsigned char *der;
    int derSz;
    wc_Sha256 sha;
    unsigned char hash[WC_SHA256_DIGEST_SIZE];
    int i;

    if (outLen < (int)(WC_SHA256_DIGEST_SIZE * 2 + 1)) return -1;

    cert = wolfSSL_get_peer_certificate(ssl);
    if (cert == NULL) return -1;

    der = wolfSSL_X509_get_der(cert, &derSz);
    if (der == NULL || derSz <= 0) {
        wolfSSL_X509_free(cert);
        return -1;
    }

    if (wc_InitSha256(&sha) != 0) {
        wolfSSL_X509_free(cert);
        return -1;
    }
    wc_Sha256Update(&sha, der, (word32)derSz);
    wc_Sha256Final(&sha, hash);
    wolfSSL_X509_free(cert);

    for (i = 0; i < WC_SHA256_DIGEST_SIZE; i++) {
        sprintf(out + (i * 2), "%02X", hash[i]);
    }
    out[WC_SHA256_DIGEST_SIZE * 2] = '\0';
    return 0;
}

static int prompt_trust(const char *hostport, const char *fp)
{
    char line[16];
    printf("TLS fingerprint for %s:\n", hostport);
    printf("SHA256 %s\n", fp);
    printf("Trust and save? (y/N): ");
    if (fgets(line, sizeof(line), stdin) == NULL) return 0;
    return (line[0] == 'y' || line[0] == 'Y');
}

static int update_known_hosts_file(const char *hostport, const char *fp)
{
    FILE *in = fopen(NOS_KNOWN_HOSTS_PATH, "r");
    FILE *out = fopen(NOS_KNOWN_HOSTS_TMP, "w");
    char line[512];
    int replaced = 0;

    if (out == NULL) {
        if (in) fclose(in);
        return -1;
    }

    if (in != NULL) {
        while (fgets(line, sizeof(line), in)) {
            char hostportTok[256] = {0};
            char algoTok[32] = {0};
            char fpTok[128] = {0};
            char lineCopy[512];
            strcpy(lineCopy, line);
            rstrip_newline(lineCopy);

            if (lineCopy[0] == '\0' || lineCopy[0] == '#') {
                fputs(line, out);
                continue;
            }

            if (sscanf(lineCopy, "%255s %31s %127s", hostportTok, algoTok, fpTok) == 3) {
                if (str_ieq(hostportTok, hostport)) {
                    fprintf(out, "%s SHA256 %s\n", hostport, fp);
                    replaced = 1;
                    continue;
                }
            }

            fputs(line, out);
        }
        fclose(in);
    }

    if (!replaced) {
        fprintf(out, "%s SHA256 %s\n", hostport, fp);
    }
    fclose(out);

    remove(NOS_KNOWN_HOSTS_PATH);
    if (rename(NOS_KNOWN_HOSTS_TMP, NOS_KNOWN_HOSTS_PATH) != 0) {
        return -1;
    }
    return 0;
}

static int check_known_host(const char *host, unsigned int port, const char *fp)
{
    char hostport[256];
    FILE *f;
    char line[512];
    int found = 0;
    char storedFp[128] = {0};

    sprintf(hostport, "%s:%u", host, port);

    f = fopen(NOS_KNOWN_HOSTS_PATH, "r");
    if (f != NULL) {
        while (fgets(line, sizeof(line), f)) {
            char hostportTok[256] = {0};
            char algoTok[32] = {0};
            char fpTok[128] = {0};

            rstrip_newline(line);
            if (line[0] == '\0' || line[0] == '#') continue;

            if (sscanf(line, "%255s %31s %127s", hostportTok, algoTok, fpTok) == 3) {
                if (str_ieq(hostportTok, hostport)) {
                    found = 1;
                    if (!str_ieq(algoTok, "SHA256")) {
                        fclose(f);
                        fprintf(stderr, "Known host entry uses unsupported algorithm: %s\n", algoTok);
                        return -1;
                    }
                    strcpy(storedFp, fpTok);
                    break;
                }
            }
        }
        fclose(f);
    }

    if (found) {
        if (str_ieq(storedFp, fp)) return 0;
        printf("WARNING: certificate fingerprint has changed for %s\n", hostport);
        if (!prompt_trust(hostport, fp)) return -1;
        if (update_known_hosts_file(hostport, fp) != 0) {
            fprintf(stderr, "Failed to update %s (read-only or disk error)\n", NOS_KNOWN_HOSTS_PATH);
            return -1;
        }
        return 0;
    }

    if (!prompt_trust(hostport, fp)) return -1;

    {
        FILE *out = fopen(NOS_KNOWN_HOSTS_PATH, "a");
        if (out == NULL) {
            fprintf(stderr, "Failed to write %s (read-only or disk error)\n", NOS_KNOWN_HOSTS_PATH);
            return -1;
        }
        fprintf(out, "%s SHA256 %s\n", hostport, fp);
        fclose(out);
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

int nos_gemini_request(const nos_url_t *url, nos_gemini_resp_t *resp,
                       char *body_buf, unsigned int body_max,
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
    if (body_buf == NULL || body_max == 0) {
        set_err(err, -1, 0, "invalid body buffer");
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
            char fingerprint[WC_SHA256_DIGEST_SIZE * 2 + 1];
            if (compute_peer_fingerprint(tls.ssl, fingerprint, sizeof(fingerprint)) != 0) {
                nos_tls_close(&tls);
                set_err(err, -1, 0, "fingerprint failed");
                return -1;
            }
            if (check_known_host(current.host, current.port, fingerprint) != 0) {
                nos_tls_close(&tls);
                set_err(err, -1, 0, "untrusted certificate");
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
                if (total >= body_max) {
                    nos_tls_close(&tls);
                    set_err(err, -1, 0, "body too large");
                    return -1;
                }
                bytes = wolfSSL_read(tls.ssl, body_buf + total, (int)(body_max - total));
                if (bytes <= 0) break;
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
