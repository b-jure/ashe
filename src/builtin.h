#ifndef __ASH_BUILTIN_H__
#define __ASH_BUILTIN_H__

#include "ashe_utils.h"

#include <sys/types.h>

int run_builtin(const byte *command, byte *const *argv, bool shell);
bool is_builtin(const byte *command);

#endif
