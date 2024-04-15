#ifndef ACOMMON_H
#define ACOMMON_H

#include "aconf.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* compiler defines */
#if defined(__GNUC__)
#define ASHE_NORET	    void __attribute__((noreturn))
#define ASHE_LIKELY(expr)   __glibc_likely(expr)
#define ASHE_UNLIKELY(expr) __glibc_unlikely(expr)
#define ASHE_MAX(a, b)                  \
	__extension__({                 \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})
#define ASHE_MIN(a, b)                  \
	__extension__({                 \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _b : _a;      \
	})
#else
#define ASHE_NORET	    void
#define ASHE_LIKELY(expr)   (expr)
#define ASHE_UNLIKELY(expr) (expr)
#define ASHE_MAX(a, b)	    ((a) > (b) ? (a) : (b))
#define ASHE_MIN(a, b)	    ((a) > (b) ? (b) : (a))
#endif // __GNUC__

#if __STDC_VERSION__ < 199901L
#if __GNUC__ >= 2
#define __func__ __FUNCTION__
#endif
#endif // __STDC_VERSION__
/* -------------------------------- */

/* function signature markers */
#define ASHE_PRIVATE static
#define ASHE_PUBLIC  extern
/* -------------------------------- */

/* private shell variables */
#define ASHE_VAR_STATUS	  "?"
#define ASHE_VAR_PID	  "$"
#define ASHE_VAR_STATUS_C '?'
#define ASHE_VAR_PID_C	  '$'
/* -------------------------------- */

/* ------ integer typedefs ------ */
typedef int8_t a_byte;
typedef uint8_t a_ubyte;
typedef int16_t a_int16;
typedef uint16_t a_uint16;
typedef int32_t a_int32;
typedef uint32_t a_uint32;
typedef int64_t a_int64;
typedef uint64_t a_uint64;

typedef size_t a_memmax;
typedef ssize_t a_ssize;
/* -------------------------------- */

/* process ID */
typedef pid_t a_pid;
/* -------------------------------- */

/* signal handler signature */
typedef void (*a_sighandler)(int);
/* -------------------------------- */

/* Environment variable valid name characters (subset of PCS) */
#define ENV_VAR_CHARS "0123456789_qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM"
/* -------------------------------- */

/* elements of array */
#define ASHE_ELEMENTS(arr) (sizeof(arr) / sizeof(arr[0]))
/* -------------------------------- */

/* 'mark' variable as unused */
#define ASHE_UNUSED(x) (void)(x)
/* -------------------------------- */

/* check if 'x' is power of 2 */
#define ASHE_ISPOW2(x) (((x) & ((x)-1)) == 0)
/* -------------------------------- */

/* string literal size excluding null term */
#define SS(str) (sizeof(str) - 1)
/* -------------------------------- */

/* max length of the conversion of a number to a string */
#define ASHE_MAXNUM2STR 44
/* -------------------------------- */

/* formats */
#define ASHE_DOUBLE_FMT "%.7g"
#define ASHE_NUMBER_FMT "%zd"
#define ASHE_PTR_FMT	"%p"
/* -------------------------------- */

/* defer */
#define ASHE_DEFER_NO_STATUS() goto defer;
#define ASHE_DEFER(n)       \
	do {                \
		status = n; \
		goto defer; \
	} while (0)
/* -------------------------------- */

#endif
