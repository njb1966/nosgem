/* config.h -- NOSgem config loader (C89)
 */

#ifndef NOS_CONFIG_H
#define NOS_CONFIG_H

#define NOS_CONFIG_PATH "C:\\NOSGEM\\NOSGEM.CFG"
#define NOS_HOMEPAGE_PATH "C:\\NOSGEM\\HOMEPAGE.GEM"

typedef struct {
    char home[256];
    unsigned int page_lines;
    unsigned long max_body;
} nos_config_t;

void nos_config_defaults(nos_config_t *cfg);
int nos_config_load(nos_config_t *cfg);

#endif /* NOS_CONFIG_H */
