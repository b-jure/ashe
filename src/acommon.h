#ifndef ACOMMON_H
#define ACOMMON_H

#include "aconf.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* compiler defines */
#if defined(__GNUC__)
#define a_noret	       void __attribute__((noreturn))
#define likely(expr)   __glibc_likely(expr)
#define unlikely(expr) __glibc_unlikely(expr)
#define MAX(a, b)                       \
	__extension__({                 \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})
#define MIN(a, b)                       \
	__extension__({                 \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _b : _a;      \
	})
#else
#define a_noret	       void
#define likely(expr)   (expr)
#define unlikely(expr) (expr)
#define MAX(a, b)      ((a) > (b) ? (a) : (b))
#define MIN(a, b)      ((a) > (b) ? (b) : (a))
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
typedef int8_t byte;
typedef uint8_t ubyte;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

typedef size_t memmax;
typedef ssize_t ssize;

typedef pid_t pid;
typedef void (*sighandler)(int);
/* -------------------------------- */

/* Environment variable valid name characters (subset of PCS) */
#define ENV_VAR_CHARS \
	"0123456789_qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM"
/* -------------------------------- */

/* elements of array */
#define ELEMENTS(arr) (sizeof(arr) / sizeof(arr[0]))
/* -------------------------------- */

/* 'mark' variable as unused */
#define unused(x) (void)(x)
/* -------------------------------- */

/* check if 'x' is power of 2 */
#define ispow2(x) (((x) & ((x)-1)) == 0)
/* -------------------------------- */

/* 64-bit signed/unsigned integer digits */
#define UINT_DIGITS 20
#define INT_DIGITS  10
/* -------------------------------- */

/* defer */
#define defer_no_status() goto defer;
#define defer(n)            \
	do {                \
		status = n; \
		goto defer; \
	} while (0)
/* -------------------------------- */

#endif
