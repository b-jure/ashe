#ifndef ABUILTIN_H
#define ABUILTIN_H

#include "acommon.h"

int run_builtin(const char *command, char *const *argv, ubyte shell);
ubyte is_builtin(const char *command);

 
#endif
