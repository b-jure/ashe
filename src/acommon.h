#ifndef ACOMMON_H
#define ACOMMON_H

#include "aconf.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>


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

/* Connection type for jobs and pipelines */
typedef enum {
	CON_AND = 2,
	CON_OR = 4,
	CON_NONE = 8,
} Connection;

/* Environment variable valid name characters (subset of PCS) */
#define ENV_VAR_CHARS \
	"0123456789_qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM"

/* Miscellaneous macros */
#define unused(x) (void)(x);
#define ispow2(x) (((x) & ((x)-1)) == 0)
#define UINT_DIGITS 20
/* -------------------------------- */

/* Function visibility */
#define ASHE_PRIVATE static
#define ASHE_PUBLIC extern

/* Compiler intrinsics */
#if defined(__GNUC__)
#define likely(expr) __glibc_likely(expr)
#define unlikely(expr) __glibc_unlikely(expr)
#define finline __attribute__((always_inline))
#define MAX(a, b)                       \
	({                              \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})
#define MIN(a, b)                       \
	({                              \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _b : _a;      \
	})
#else
#define likely(expr)
#define unlikely(expr)
#define finline inline
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif // __GNUC__
/* -------------------------------- */

/* ---- Assertions ---- */
#ifndef ashe_assert
#undef NDBG
#include <assert.h>
#define ashe_assert(expr, msg) assert((expr) && (msg))
#endif
/* -------------------------------- */

#endif
