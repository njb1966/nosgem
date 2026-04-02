/* nav.c -- Navigation stack (C89)
 */

#include <string.h>
#include "nav.h"

void nos_nav_init(nos_nav_t *nav)
{
    if (nav == NULL) return;
    nav->count = 0;
}

int nos_nav_push(nos_nav_t *nav, const nos_url_t *url)
{
    if (nav == NULL || url == NULL) return -1;
    if (nav->count >= NOS_NAV_MAX) return -1;
    nav->items[nav->count++] = *url;
    return 0;
}

int nos_nav_pop(nos_nav_t *nav, nos_url_t *out)
{
    if (nav == NULL || out == NULL) return -1;
    if (nav->count == 0) return -1;
    nav->count--;
    *out = nav->items[nav->count];
    return 0;
}

int nos_nav_peek(const nos_nav_t *nav, nos_url_t *out)
{
    if (nav == NULL || out == NULL) return -1;
    if (nav->count == 0) return -1;
    *out = nav->items[nav->count - 1];
    return 0;
}

unsigned int nos_nav_count(const nos_nav_t *nav)
{
    if (nav == NULL) return 0;
    return nav->count;
}
