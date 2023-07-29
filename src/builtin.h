#ifndef __ASH_BUILTIN_H__
#define __ASH_BUILTIN_H__

#include "ashe_utils.h"

#include <sys/types.h>

int exit_builtin(byte *const *argv, bool shell);
int fg(pid_t pgid, int id);

#endif
