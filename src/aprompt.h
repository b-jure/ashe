#ifndef APROMPT_H
#define APROMPT_H

#include "acommon.h"
#include "atoken.h"

void parsestring(Buffer *out, const char *str);
void print_userstr(const char *str, memmax len, uint32 bufidx);
#define print_welcome() print_userstr(ASHE_WELCOME, sizeof(ASHE_WELCOME), 0)
#define print_prompt()	print_userstr(ASHE_PROMPT, sizeof(ASHE_PROMPT), 1)

#endif
