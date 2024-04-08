#include "aalloc.h"
#include "aconf.h"
#include "ajobcntl.h"
#include "ashell.h"
#include "autils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

ASHE_PUBLIC void ashe_cleanup(void)
{
	a_jobcntl_harvest(&ashe.sh_jobcntl);
	a_shell_free(&ashe);
}

ASHE_PUBLIC void ashe_cleanupfork(void)
{
	a_shell_free(&ashe);
}

ASHE_PRIVATE void vprintf_panic(const char *errmsg, va_list argp)
{
	const char *where;

	ashe_print(ASHE_PANIC_PREFIX, stderr); /* memleak 'va_list' on panic */
	where = va_arg(argp, const char *);
	ashe_printf(stderr, "%s: ", where);
	if (errmsg)
		ashe_vprintf(stderr, errmsg, argp);
	else
		ashe_print("...", stderr);
	ashe_print("\r\n", stderr);
}

ASHE_PUBLIC a_noret ashe_internal_panic(ubyte direct, const char *errmsg, ...)
{
	va_list argp;

	if (!direct || (direct && errmsg != NULL)) {
		va_start(argp, errmsg);
		vprintf_panic(errmsg, argp);
		va_end(argp);
	}
	if (ashe.sh_flags.isfork) {
		ashe_cleanupfork();
		_exit(EXIT_FAILURE);
	} else {
		ashe_cleanup();
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
		ashe_perrno(NULL);
		ashe_internal_panic(1, NULL);
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
