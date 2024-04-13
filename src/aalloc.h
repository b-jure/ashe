#ifndef AALLOC_H
#define AALLOC_H

#include "acommon.h"

#include <errno.h>

void *ashe_realloc(void *ptr, a_memmax size);
#define ashe_malloc(size) ashe_realloc(NULL, size)
void *ashe_calloc(a_memmax elem, a_memmax size);
void ashe_free(void *ptr);

void ashe_cleanup(void);
void ashe_cleanupfork(void);

#define APANIC_CALL	0
#define APANIC_LIBCALL	1
#define APANIC_LIBWCALL 2
#define APANIC_ABORT	4
/* never call this directly (use macros instead) */
ASHE_NORET ashe_internal_panic(const char *errmsg, a_int32 apanic, ...);
/* panic source information */
#define ashe_panic_source __FILE__, __LINE__, __func__
/* panic error info */
#define ashe_errno_info (errno ? strerror(errno) : "<no info>")
/* panic format */
#define ashe_panic_fmt(fmt)   (fmt ? fmt : "<no info>")
#define ashe_panic_callee(fn) (#fn ? #fn : "?")
#define ashe_libcall_fmt      "libc function '%s' errored: %s"
#define ashe_libwcall_fmt     "libc wrapper function %s' errored: "
/* helper macro defs */
#define ashe_internal_panic_arg(fmt, type) \
	ashe_internal_panic(ashe_panic_fmt(fmt), type, ashe_panic_source)
#define ashe_internal_panic_args(callee, fmt, type, extra) \
	ashe_internal_panic(ashe_panic_fmt(fmt), type, ashe_panic_source, callee, extra)

/*
 * ONLY USE MACROS BELOW FOR INVOKING PANIC !
 */

/* generic panic */
#define ashe_panic(err) ashe_internal_panic_arg(err, APANIC_CALL);

/* generic panic with formatting */
#define ashe_panicf(errfmt, ...) \
	ashe_internal_panic(errfmt, APANIC_CALL, ashe_panic_source, __VA_ARGS__)

/* library call panic */
#define ashe_panic_libcall(libfn) \
	ashe_internal_panic_args(#libfn, ashe_libcall_fmt, APANIC_LIBCALL, ashe_errno_info)

/* wrapped libcall panic */
#define ashe_panic_libwcall(libwfn, extra) \
	ashe_internal_panic_args(#libwfn, ashe_libwcall_fmt, APANIC_LIBWCALL, extra)

/* panic that aborts without printing anything */
#define ashe_panic_abort() ashe_internal_panic(NULL, APANIC_ABORT)

#endif
