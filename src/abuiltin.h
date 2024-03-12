#ifndef ABUILTIN_H
#define ABUILTIN_H

#include "acommon.h"
#include "aarray.h"

ARRAY_NEW(ArrayCharptr, char*);

int run_builtin(ArrayCharptr* argv);
ubyte is_builtin(const char *command);

 
#endif
