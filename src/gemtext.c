/* gemtext.c -- Gemtext line parsing (C89)
 */

#include <string.h>
#include "gemtext.h"

static void trim_left(const char **p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

int nos_gemtext_parse_line(const char *line, int *in_pre, nos_gemtext_line_t *out)
{
    const char *p;

    if (line == NULL || in_pre == NULL || out == NULL) return -1;

    out->text[0] = '\0';
    out->url[0] = '\0';

    if (strncmp(line, "```", 3) == 0) {
        *in_pre = !(*in_pre);
        out->type = NOS_GEMTEXT_PREFORMAT;
        out->text[0] = '\0';
        return 0;
    }

    if (*in_pre) {
        out->type = NOS_GEMTEXT_PREFORMAT;
        if (strlen(line) >= sizeof(out->text)) return -1;
        strcpy(out->text, line);
        return 0;
    }

    if (line[0] == '#' ) {
        if (line[1] == '#' && line[2] == '#') {
            out->type = NOS_GEMTEXT_H3;
            p = line + 3;
        } else if (line[1] == '#') {
            out->type = NOS_GEMTEXT_H2;
            p = line + 2;
        } else {
            out->type = NOS_GEMTEXT_H1;
            p = line + 1;
        }
        trim_left(&p);
        if (strlen(p) >= sizeof(out->text)) return -1;
        strcpy(out->text, p);
        return 0;
    }

    if (line[0] == '=' && line[1] == '>') {
        out->type = NOS_GEMTEXT_LINK;
        p = line + 2;
        trim_left(&p);
        {
            const char *sp = p;
            while (*sp != '\0' && *sp != ' ' && *sp != '\t') sp++;
            if ((unsigned int)(sp - p) >= sizeof(out->url)) return -1;
            memcpy(out->url, p, (unsigned int)(sp - p));
            out->url[sp - p] = '\0';
            p = sp;
            trim_left(&p);
            if (*p) {
                if (strlen(p) >= sizeof(out->text)) return -1;
                strcpy(out->text, p);
            } else {
                out->text[0] = '\0';
            }
        }
        return 0;
    }

    if (line[0] == '*' && line[1] == ' ') {
        out->type = NOS_GEMTEXT_LIST;
        p = line + 2;
        if (strlen(p) >= sizeof(out->text)) return -1;
        strcpy(out->text, p);
        return 0;
    }

    if (line[0] == '>') {
        out->type = NOS_GEMTEXT_QUOTE;
        p = line + 1;
        trim_left(&p);
        if (strlen(p) >= sizeof(out->text)) return -1;
        strcpy(out->text, p);
        return 0;
    }

    out->type = NOS_GEMTEXT_TEXT;
    if (strlen(line) >= sizeof(out->text)) return -1;
    strcpy(out->text, line);
    return 0;
}
