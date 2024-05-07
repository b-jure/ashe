#ifndef APROMPT_H
#define APROMPT_H

#include "acommon.h"
#include "atoken.h"

#define ASHE_USERSTR_MAX ((ARG_MAX >> 2) ? (ARG_MAX >> 2) : 1024)

void ashe_puserstr(const char *str, a_memmax len);
void ashe_pwelcome(void);
void parse_placeholders(a_arr_char *out, const char *str);

#endif
