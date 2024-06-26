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

/* max lenghts of number to string conversions */
#define ASHE_MAXINT8STR	 5
#define ASHE_MAXINT16STR 7
#define ASHE_MAXINT32STR 12
#define ASHE_MAXINT64STR 22
#define ASHE_MAXNUMSTR	 44
/* -------------------------------- */

/* formats */
#define ASHE_DOUBLE_FMT "%.7g"
#define ASHE_NUMBER_FMT "%zd"
#define ASHE_PTR_FMT	"%p"
/* -------------------------------- */

/* defer */
#define ASHE_DEFER(n)       \
	do {                \
		status = n; \
		goto defer; \
	} while (0)
/* -------------------------------- */

#endif
