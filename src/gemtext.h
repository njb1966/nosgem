/* gemtext.h -- Gemtext line parsing (C89)
 */

#ifndef NOS_GEMTEXT_H
#define NOS_GEMTEXT_H

typedef enum {
    NOS_GEMTEXT_TEXT = 0,
    NOS_GEMTEXT_LINK,
    NOS_GEMTEXT_H1,
    NOS_GEMTEXT_H2,
    NOS_GEMTEXT_H3,
    NOS_GEMTEXT_LIST,
    NOS_GEMTEXT_QUOTE,
    NOS_GEMTEXT_PREFORMAT
} nos_gemtext_type_t;

typedef struct {
    nos_gemtext_type_t type;
    char text[1024];
    char url[512];
} nos_gemtext_line_t;

/*
 * nos_gemtext_parse_line -- parse a single gemtext line.
 * in_pre toggles on ``` lines.
 * Returns 0 on success, -1 on parse error.
 */
int nos_gemtext_parse_line(const char *line, int *in_pre, nos_gemtext_line_t *out);

#endif /* NOS_GEMTEXT_H */
