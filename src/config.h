#ifndef __ASH_CONFIG_H__
#define __ASH_CONFIG_H__

#include "ashe_string.h"
#include "ashe_utils.h"

#define CV_WELCOME 0
#define CV_PROMPT 1

string_t *config_getvar(int fd, int varidx);
const byte *config_var(int vardix);
int config_open(void);

#endif
