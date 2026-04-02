/* render.c -- Gemtext renderer for DOS text mode (C89)
 */

#include <stdio.h>
#include <string.h>
#include "render.h"
#include "gemtext.h"

#define NOS_WRAP_COLS 80

static void print_wrapped(const char *prefix, const char *text)
{
    unsigned int col = 0;
    const char *p = text;
    char word[256];

    if (prefix && *prefix) {
        fputs(prefix, stdout);
        col = (unsigned int)strlen(prefix);
    }

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        {
            unsigned int wlen = 0;
            while (p[wlen] != '\0' && p[wlen] != ' ' && p[wlen] != '\t') {
                if (wlen + 1 < sizeof(word)) {
                    word[wlen] = p[wlen];
                }
                wlen++;
            }
            if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
            word[wlen] = '\0';

            if (col == 0 && prefix && *prefix) {
                fputs(prefix, stdout);
                col = (unsigned int)strlen(prefix);
            }

            if (col + (col ? 1 : 0) + wlen > NOS_WRAP_COLS) {
                fputc('\n', stdout);
                if (prefix && *prefix) {
                    fputs(prefix, stdout);
                    col = (unsigned int)strlen(prefix);
                } else {
                    col = 0;
                }
            } else if (col > 0) {
                fputc(' ', stdout);
                col++;
            }

            fputs(word, stdout);
            col += wlen;
            p += wlen;
        }
    }
    fputc('\n', stdout);
}

static void add_link(nos_render_ctx_t *ctx, const char *url, const char *label)
{
    if (ctx == NULL) return;
    if (ctx->link_count >= NOS_MAX_LINKS) return;

    strncpy(ctx->links[ctx->link_count].url, url, sizeof(ctx->links[ctx->link_count].url) - 1);
    ctx->links[ctx->link_count].url[sizeof(ctx->links[ctx->link_count].url) - 1] = '\0';

    strncpy(ctx->links[ctx->link_count].label, label, sizeof(ctx->links[ctx->link_count].label) - 1);
    ctx->links[ctx->link_count].label[sizeof(ctx->links[ctx->link_count].label) - 1] = '\0';

    ctx->link_count++;
}

int nos_render_gemtext(const char *body, unsigned int len, nos_render_ctx_t *ctx)
{
    unsigned int i = 0;
    unsigned int lineLen = 0;
    char line[1024];
    int in_pre = 0;
    nos_gemtext_line_t parsed;

    if (body == NULL || ctx == NULL) return -1;
    ctx->link_count = 0;

    while (i < len) {
        char c = body[i++];
        if (c == '\r') continue;
        if (c == '\n' || i == len) {
            if (c != '\n' && i == len && lineLen + 1 < sizeof(line)) {
                line[lineLen++] = c;
            }
            line[lineLen] = '\0';

            if (nos_gemtext_parse_line(line, &in_pre, &parsed) != 0) return -1;

            switch (parsed.type) {
                case NOS_GEMTEXT_H1:
                    fputc('\n', stdout);
                    print_wrapped("# ", parsed.text);
                    break;
                case NOS_GEMTEXT_H2:
                    fputc('\n', stdout);
                    print_wrapped("## ", parsed.text);
                    break;
                case NOS_GEMTEXT_H3:
                    fputc('\n', stdout);
                    print_wrapped("### ", parsed.text);
                    break;
                case NOS_GEMTEXT_LINK: {
                    char label[256];
                    if (parsed.text[0]) {
                        strncpy(label, parsed.text, sizeof(label) - 1);
                        label[sizeof(label) - 1] = '\0';
                    } else {
                        strncpy(label, parsed.url, sizeof(label) - 1);
                        label[sizeof(label) - 1] = '\0';
                    }
                    add_link(ctx, parsed.url, label);
                    {
                        char prefix[16];
                        sprintf(prefix, "[%u] ", ctx->link_count);
                        print_wrapped(prefix, label);
                    }
                    break;
                }
                case NOS_GEMTEXT_LIST:
                    print_wrapped("* ", parsed.text);
                    break;
                case NOS_GEMTEXT_QUOTE:
                    print_wrapped("> ", parsed.text);
                    break;
                case NOS_GEMTEXT_PREFORMAT:
                    if (parsed.text[0] == '\0') {
                        fputc('\n', stdout);
                    } else {
                        fputs(parsed.text, stdout);
                        fputc('\n', stdout);
                    }
                    break;
                case NOS_GEMTEXT_TEXT:
                default:
                    print_wrapped("", parsed.text);
                    break;
            }

            lineLen = 0;
        } else {
            if (lineLen + 1 < sizeof(line)) {
                line[lineLen++] = c;
            } else {
                return -1;
            }
        }
    }

    return 0;
}
