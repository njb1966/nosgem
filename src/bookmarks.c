/* bookmarks.c -- simple bookmarks file (C89)
 */

#include <stdio.h>
#include <string.h>
#include "bookmarks.h"

int nos_bookmarks_add(const char *url, const char *label)
{
    FILE *f;
    if (url == NULL || *url == '\0') return -1;

    f = fopen(NOS_BOOKMARKS_PATH, "a");
    if (f == NULL) return -1;

    if (label && *label) {
        fprintf(f, "=> %s %s\n", url, label);
    } else {
        fprintf(f, "=> %s\n", url);
    }

    fclose(f);
    return 0;
}
