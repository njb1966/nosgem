/* config.c -- NOSgem config loader (C89)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "config.h"

static void trim(char *s)
{
    char *p = s;
    char *end;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end[-1] = '\0';
        end--;
    }
}

void nos_config_defaults(nos_config_t *cfg)
{
    if (cfg == NULL) return;
    strcpy(cfg->home, "gemini://geminiprotocol.net/");
    cfg->page_lines = 24;
    cfg->max_body = 524288UL;
}

int nos_config_load(nos_config_t *cfg)
{
    FILE *f;
    char line[256];

    if (cfg == NULL) return -1;

    f = fopen(NOS_CONFIG_PATH, "r");
    if (f == NULL) return -1;

    while (fgets(line, sizeof(line), f)) {
        char *eq;
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        eq++;
        trim(line);
        trim(eq);

        if (strcmp(line, "HOME") == 0) {
            strncpy(cfg->home, eq, sizeof(cfg->home) - 1);
            cfg->home[sizeof(cfg->home) - 1] = '\0';
        } else if (strcmp(line, "PAGE_LINES") == 0) {
            cfg->page_lines = (unsigned int)atoi(eq);
        } else if (strcmp(line, "MAX_BODY") == 0) {
            cfg->max_body = (unsigned long)strtoul(eq, NULL, 10);
        }
    }

    fclose(f);
    return 0;
}
