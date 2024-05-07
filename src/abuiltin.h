#ifndef ABUILTIN_H
#define ABUILTIN_H

#include "acommon.h"
#include "aarray.h"
#include "aparser.h"

enum a_builtin_type {
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

a_int32 ashe_runbin(struct a_simple_cmd *scmd, enum a_builtin_type bi);
a_int32 ashe_isbin(const char *command);

#endif
