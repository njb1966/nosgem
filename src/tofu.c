/* tofu.c -- TOFU certificate trust store (C89)
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "tofu.h"
#include <wolfssl/wolfcrypt/sha256.h>

#define NOS_KNOWN_HOSTS_PATH "C:\\known_hosts.txt"
#define NOS_KNOWN_HOSTS_TMP  "C:\\known_hosts.tmp"

static void set_err(char *err, unsigned int errMax, const char *msg)
{
    if (err == NULL || errMax == 0) return;
    if (msg == NULL) {
        err[0] = '\0';
        return;
    }
    strncpy(err, msg, errMax - 1);
    err[errMax - 1] = '\0';
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

static int check_known_host(const char *host, unsigned int port, const char *fp, char *err, unsigned int errMax)
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
                        set_err(err, errMax, "unsupported fingerprint algorithm");
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
        if (!prompt_trust(hostport, fp)) {
            set_err(err, errMax, "certificate changed");
            return -1;
        }
        if (update_known_hosts_file(hostport, fp) != 0) {
            set_err(err, errMax, "failed to update known_hosts");
            return -1;
        }
        return 0;
    }

    if (!prompt_trust(hostport, fp)) {
        set_err(err, errMax, "certificate not trusted");
        return -1;
    }

    {
        FILE *out = fopen(NOS_KNOWN_HOSTS_PATH, "a");
        if (out == NULL) {
            set_err(err, errMax, "failed to write known_hosts");
            return -1;
        }
        fprintf(out, "%s SHA256 %s\n", hostport, fp);
        fclose(out);
    }
    return 0;
}

int nos_tofu_check(WOLFSSL *ssl, const char *host, unsigned int port,
                   char *err, unsigned int errMax)
{
    char fingerprint[WC_SHA256_DIGEST_SIZE * 2 + 1];

    if (ssl == NULL || host == NULL) {
        set_err(err, errMax, "invalid arguments");
        return -1;
    }

    if (compute_peer_fingerprint(ssl, fingerprint, sizeof(fingerprint)) != 0) {
        set_err(err, errMax, "fingerprint failed");
        return -1;
    }

    if (check_known_host(host, port, fingerprint, err, errMax) != 0) return -1;

    return 0;
}
