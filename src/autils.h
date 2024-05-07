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

#ifndef AUTILS_H
#define AUTILS_H

#include <stdarg.h>
#include <stdio.h>

#include "atoken.h"
#include "acommon.h"

#define a_fd_isopen(fd, bitmask) ((bitmask) & fcntl(fd, F_GETFL))

/* push string literal */
#define a_arr_char_push_strlit(out, lit) a_arr_char_push_str(out, lit, SS(lit))

/* push format */
void a_arr_char_push_strf(a_arr_char *buffer, const char *fmt, ...);
void a_arr_char_push_vstrf(a_arr_char *buffer, const char *fmt, va_list argp);

/* generic print (libc) */
void ashe_print(const char *msg, FILE *stream);
void ashe_printf(FILE *stream, const char *msg, ...);
void ashe_vprintf(FILE *stream, const char *msg, va_list argp);

/*
 * ashe print functions, these accept
 * minimal set of format specifiers as
 * specified in [autils.c: a_arr_char_push_strf()].
 */
void ashe_eprintf(const char *errfmt, ...);
void ashe_perrno(const char *errfmt, ...);
void ashe_pinfo(const char *ifmt, ...);
#ifdef ASHE_DBG
void ashe_dprintf(const char *dfmt, ...);
#define ashe_dprint(str) ashe_dprintf(str)
#else
#define ashe_dprintf(dftm, ...) ((void)(0))
#define ashe_dprint(str)	((void)(0))
#endif

/* duplicate string/bytes */
char *ashe_dupstr(const char *str);
char *ashe_dupstrn(const char *str, a_memmax len);

/* check for double quotes or escape character */
a_ubyte ashe_indq(const char *str, a_memmax len);
a_ubyte ashe_isescaped(const char *s, a_memmax curpos);

/* length without escape sequences */
a_memmax ashe_noescseq_len(const char *str);

/* buffer processing */
void ashe_unescape(a_arr_char *buffer, a_uint32 from, a_uint32 to);
void ashe_escape(a_arr_char *buffer);
void ashe_expandvars(a_arr_char *buffer);

#endif
