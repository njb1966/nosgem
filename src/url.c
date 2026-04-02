/* url.c -- Gemini URL parsing and resolution (C89)
 */

#include <string.h>
#include <ctype.h>
#include "url.h"

#define NOS_URL_SCHEME "gemini://"
#define NOS_DEFAULT_PORT 1965

static int str_ieq_n(const char *a, const char *b, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca == '\0' || cb == '\0') return 0;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return 1;
}

static int is_all_digits(const char *s)
{
    if (s == NULL || *s == '\0') return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

static int normalize_path(char *path, unsigned int maxLen)
{
    char temp[512];
    unsigned int out = 0;
    unsigned int i = 0;
    unsigned int segStarts[128];
    unsigned int segCount = 0;

    if (maxLen == 0) return -1;

    while (path[i] != '\0') {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        {
            unsigned int segStart = i;
            unsigned int segLen = 0;
            while (path[i] != '\0' && path[i] != '/') { i++; segLen++; }

            if (segLen == 1 && path[segStart] == '.') {
                continue;
            }
            if (segLen == 2 && path[segStart] == '.' && path[segStart + 1] == '.') {
                if (segCount > 0) {
                    out = segStarts[segCount - 1];
                    temp[out] = '\0';
                    segCount--;
                }
                continue;
            }

            if (out + segLen + 1 >= sizeof(temp)) return -1;
            if (out == 0) {
                temp[out++] = '/';
            } else {
                temp[out++] = '/';
            }
            segStarts[segCount++] = out - 1;

            memcpy(&temp[out], &path[segStart], segLen);
            out += segLen;
            temp[out] = '\0';

            if (segCount >= (sizeof(segStarts) / sizeof(segStarts[0]))) return -1;
        }
    }

    if (out == 0) {
        temp[out++] = '/';
        temp[out] = '\0';
    }

    if (out + 1 > maxLen) return -1;
    strcpy(path, temp);
    return 0;
}

int nos_url_parse(const char *raw, nos_url_t *out)
{
    const char *p;
    const char *hostStart;
    const char *hostEnd;
    const char *portSep;
    const char *pathStart;
    unsigned int hostLen;
    unsigned long portVal;

    if (raw == NULL || out == NULL) return -1;

    out->host[0] = '\0';
    out->path[0] = '\0';
    out->port = NOS_DEFAULT_PORT;

    if (str_ieq_n(raw, NOS_URL_SCHEME, (unsigned int)strlen(NOS_URL_SCHEME))) {
        p = raw + strlen(NOS_URL_SCHEME);
    } else {
        p = raw;
        if (strstr(raw, "://") != NULL) return -1;
    }

    hostStart = p;
    while (*p != '\0' && *p != '/') p++;
    hostEnd = p;

    if (hostEnd == hostStart) return -1;

    pathStart = (*p == '/') ? p : NULL;

    portSep = NULL;
    {
        const char *c = hostStart;
        while (c < hostEnd) {
            if (*c == ':') { portSep = c; break; }
            c++;
        }
    }

    if (portSep) {
        hostLen = (unsigned int)(portSep - hostStart);
        if (hostLen == 0 || hostLen >= sizeof(out->host)) return -1;
        memcpy(out->host, hostStart, hostLen);
        out->host[hostLen] = '\0';

        if (!is_all_digits(portSep + 1)) return -1;
        portVal = 0;
        {
            const char *d = portSep + 1;
            while (d < hostEnd) {
                portVal = portVal * 10 + (unsigned long)(*d - '0');
                if (portVal > 65535UL) return -1;
                d++;
            }
        }
        if (portVal == 0) return -1;
        out->port = (unsigned int)portVal;
    } else {
        hostLen = (unsigned int)(hostEnd - hostStart);
        if (hostLen == 0 || hostLen >= sizeof(out->host)) return -1;
        memcpy(out->host, hostStart, hostLen);
        out->host[hostLen] = '\0';
    }

    if (pathStart && *pathStart) {
        if (strlen(pathStart) >= sizeof(out->path)) return -1;
        strcpy(out->path, pathStart);
    } else {
        strcpy(out->path, "/");
    }

    return normalize_path(out->path, sizeof(out->path));
}

int nos_url_resolve(const nos_url_t *base, const char *link, nos_url_t *out)
{
    char temp[512];
    unsigned int baseLen;
    const char *slash;

    if (base == NULL || link == NULL || out == NULL) return -1;

    if (link[0] == '\0') {
        *out = *base;
        return 0;
    }

    if (str_ieq_n(link, NOS_URL_SCHEME, (unsigned int)strlen(NOS_URL_SCHEME))) {
        return nos_url_parse(link, out);
    }
    if (strstr(link, "://") != NULL) return -1;

    *out = *base;

    if (link[0] == '/') {
        if (strlen(link) >= sizeof(out->path)) return -1;
        strcpy(out->path, link);
        return normalize_path(out->path, sizeof(out->path));
    }

    if (link[0] == '?') {
        if (strlen(base->path) + strlen(link) >= sizeof(temp)) return -1;
        strcpy(temp, base->path);
        strcat(temp, link);
        strcpy(out->path, temp);
        return 0;
    }

    baseLen = (unsigned int)strlen(base->path);
    if (baseLen == 0) {
        strcpy(temp, "/");
    } else {
        slash = strrchr(base->path, '/');
        if (slash == NULL) {
            strcpy(temp, "/");
        } else {
            unsigned int dirLen = (unsigned int)(slash - base->path + 1);
            if (dirLen >= sizeof(temp)) return -1;
            memcpy(temp, base->path, dirLen);
            temp[dirLen] = '\0';
        }
    }

    if (strlen(temp) + strlen(link) >= sizeof(temp)) return -1;
    strcat(temp, link);

    if (strlen(temp) >= sizeof(out->path)) return -1;
    strcpy(out->path, temp);
    return normalize_path(out->path, sizeof(out->path));
}
