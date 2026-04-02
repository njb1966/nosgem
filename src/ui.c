/* ui.c -- Input handling stubs (C89)
 */

#include <stdio.h>
#include <string.h>
#include "ui.h"

int nos_ui_prompt(const char *prompt, char *buf, unsigned int max)
{
    if (buf == NULL || max == 0) return -1;
    if (prompt && *prompt) fputs(prompt, stdout);
    if (fgets(buf, max, stdin) == NULL) return -1;

    {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[len-1] = '\0';
            len--;
        }
    }

    return 0;
}
