#include "aalloc.h"
#include "aasync.h"
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

ASHE_PRIVATE void ppanic(const char *efmt, va_list argp, a_int32 apanic)
{
	const char *caller, *callee;
	const char *error;
	const char *file;
	a_int32 line;

	ashe_assert(efmt != NULL);

	/* print prefix */
	ashe_print(ASHE_PANIC_PREFIX, stderr);

	/* get source information and print it */
	file = va_arg(argp, const char *);
	ashe_assert(file != NULL);
	line = va_arg(argp, a_int32);
	caller = va_arg(argp, const char *);
	ashe_assert(caller != NULL);
	ashe_printf(stderr, "at %s on line %d in %s(): ", file, line, caller);

	/* print actual error */
	switch (apanic) {
	case APANIC_CALL:
		ashe_vprintf(stderr, efmt, argp);
		break;
	case APANIC_LIBCALL:
	case APANIC_LIBWCALL:
		callee = va_arg(argp, const char *);
		ashe_assert(callee != NULL);
		error = va_arg(argp, const char *);
		ashe_assert(error != NULL);
		ashe_printf(stderr, efmt, callee, error);
		break;
	default:
		/* UNREACHED */
		ashe_assert(0);
		break;
	}

	/* done */
	ashe_print("\r\n", stderr);
}

ASHE_PUBLIC ASHE_NORET ashe_internal_panic(const char *restrict errmsg, a_int32 apanic, ...)
{
	va_list argp;

	if (ashe.sh_flags.panic || (apanic & APANIC_ABORT))
		abort();

	ashe_disable_jobcntl_updates();
	ashe_mask_signals(SIG_BLOCK);

	ashe.sh_flags.panic = 1; /* prevent recursive panic calls */
	va_start(argp, apanic);
	ppanic(errmsg, argp, apanic);
	va_end(argp);

	ashe_exit(EXIT_FAILURE);
}

ASHE_PUBLIC void *ashe_realloc(void *ptr, a_memmax size)
{
	if (size == 0) {
		free(ptr);
		return NULL;
	}

	ptr = realloc(ptr, size);

	if (ASHE_UNLIKELY(ptr == NULL))
		ashe_panic_libcall(realloc);

	return ptr;
}

ASHE_PUBLIC void *ashe_calloc(a_memmax elem, a_memmax size)
{
	void *ptr;

	ptr = ashe_malloc(elem * size);
	memset(ptr, 0, elem * size);
	return ptr;
}

/* wrapper */
ASHE_PUBLIC void ashe_free(void *ptr)
{
	ashe_realloc(ptr, 0);
}
