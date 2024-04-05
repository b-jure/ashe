#include "aalloc.h"
#include "aconf.h"
#include "ajobcntl.h"
#include "ashell.h"
#include "autils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

ASHE_PUBLIC void cleanup_all(void)
{
	a_jobcntl_harvest(&ashe.sh_jobcntl);
	a_shell_free(&ashe);
}

ASHE_PUBLIC void cleanup_fork(void)
{
	a_shell_free(&ashe);
}

ASHE_PRIVATE void vprintf_panic(const char *errmsg, va_list argp)
{
	ashe_print(ASHE_PANIC_PREFIX, stderr); /* memleak 'va_list' on panic */
	ashe_vprintf(stderr, errmsg, argp);
	va_end(argp);
	ashe_print("\r\n", stderr);
}

ASHE_PUBLIC void panic(const char *errmsg, ...)
{
	va_list argp;

	if (errmsg) {
		va_start(argp, errmsg);
		vprintf_panic(errmsg, argp);
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
		ashe_perrno();
		panic(NULL);
	}
	return ptr;
}

ASHE_PUBLIC void *acalloc(memmax elem, memmax size)
{
	void *ptr;

	ptr = amalloc(elem * size);
	memset(ptr, 0, elem * size);
	return ptr;
}

/* wrapper */
ASHE_PUBLIC void afree(void *ptr)
{
	arealloc(ptr, 0);
}
