#ifndef ABUILTIN_H
#define ABUILTIN_H

#include "acommon.h"
#include "aarray.h"

typedef struct Command Command;

ARRAY_NEW(ArrayCharptr, char *)

enum tbi {
	TBI_BUILTIN = 0,
	TBI_BG,
	TBI_CD,
	TBI_CLEAR,
	TBI_FG,
	TBI_JOBS,
	TBI_PENV,
	TBI_PWD,
	TBI_RENV,
	TBI_SENV,
	TBI_EXEC,
	TBI_EXIT,
};

int32 run_builtin(Command *cmd, enum tbi bi);
int32 is_builtin(const char *command);

#endif
