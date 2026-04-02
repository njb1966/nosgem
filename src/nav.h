/* nav.h -- Navigation stack (C89)
 */

#ifndef NOS_NAV_H
#define NOS_NAV_H

#include "url.h"

#define NOS_NAV_MAX 32

typedef struct {
    nos_url_t items[NOS_NAV_MAX];
    unsigned int count;
} nos_nav_t;

void nos_nav_init(nos_nav_t *nav);
int nos_nav_push(nos_nav_t *nav, const nos_url_t *url);
int nos_nav_pop(nos_nav_t *nav, nos_url_t *out);
int nos_nav_peek(const nos_nav_t *nav, nos_url_t *out);
unsigned int nos_nav_count(const nos_nav_t *nav);

#endif /* NOS_NAV_H */
