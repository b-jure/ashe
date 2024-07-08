/* ----------------------------------------------------------------------------------------------
 * Copyright (C) 2023-2024 Jure Bagić
 *
 * This file is part of ashe.
 * ashe is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ashe is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ashe.
 * If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------------------------*/

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
	a_ubyte canfail;

#ifdef ASHE_DBG
	canfail = 0;
#else
	canfail = 1;
#endif
	ashe_freehistlist(&ashe.sh_history, NULL, canfail);
	a_jobcntl_harvest(&ashe.sh_jobcntl);
	a_shell_free(&ashe);
}

ASHE_PUBLIC void ashe_cleanupfork(void)
{
	a_shell_free(&ashe);
}

ASHE_PRIVATE void ppanic(const char *efmt, va_list argp, a_int32 apanic)
{
	a_arr_char buffer;
	const char *caller, *callee;
	const char *error;
	const char *file;
	a_int32 line;

	ashe_assert(efmt != NULL);
	a_arr_char_init(&buffer);
	a_arr_char_push_strlit(&buffer, ASHE_PANIC_PREFIX);

	/* source information */
	file = va_arg(argp, const char *);
	ashe_assert(file != NULL);
	line = va_arg(argp, a_int32);
	caller = va_arg(argp, const char *);
	ashe_assert(caller != NULL);
	a_arr_char_push_strf(&buffer, "at %s on line %n in %s(): ", file, line, caller);

	switch (apanic) {
	case APANIC_CALL:
		a_arr_char_push_vstrf(&buffer, efmt, argp);
		break;
	case APANIC_LIBCALL:
	case APANIC_LIBWCALL:
		callee = va_arg(argp, const char *);
		ashe_assert(callee != NULL);
		error = va_arg(argp, const char *);
		ashe_assert(error != NULL);
		a_arr_char_push_strf(&buffer, efmt, callee, error);
		break;
	default:
		/* UNREACHED */
		ashe_assert(0);
		break;
	}
	a_arr_char_push_strlit(&buffer, "\r\n\0");

	/* print all */
	ashe_print(a_arr_ptr(buffer), stderr);

	a_arr_char_free(&buffer, NULL);
}

ASHE_PUBLIC a_noret ashe_internal_panic(const char *restrict errmsg, a_int32 apanic, ...)
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

	if (a_unlikely(ptr == NULL)) {
		perror("ashe");
		ashe_panic_abort();
	}

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
