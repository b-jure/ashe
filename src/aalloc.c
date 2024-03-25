#include "aalloc.h"
#include "ajobcntl.h"
#include "ashell.h"
#include "autils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

ASHE_PUBLIC void cleanup_all(void)
{
	JobControl_free_and_harvest(&ashe.sh_jobcntl);
	ArrayCharptr_free(&ashe.sh_buffers, NULL);
	ArrayConditional_free(&ashe.sh_conds, (FreeFn)Conditional_free);
}

ASHE_PUBLIC void cleanup_fork(void)
{
	JobControl_free(&ashe.sh_jobcntl);
	ArrayCharptr_free(&ashe.sh_buffers, NULL);
	ArrayConditional_free(&ashe.sh_conds, (FreeFn)Conditional_free);
}

ASHE_PUBLIC void panic(const char *errmsg, ...)
{
	va_list argp;
	if (errmsg) {
		va_start(argp, errmsg);
		vfprintf(stderr, errmsg, argp);
		fputs("\r\n", stderr);
		va_end(argp);
	}
	if (ashe.sh_flags.isfork) {
		cleanup_fork();
		_exit(EXIT_FAILURE);
	} else {
		cleanup_all();
		exit(EXIT_FAILURE);
	}
}

ASHE_PUBLIC void *arealloc(void *ptr, memmax size)
{
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	ptr = realloc(ptr, size);
	if (unlikely(ptr == NULL)) {
		print_errno();
		panic(NULL);
	}
	return ptr;
}
