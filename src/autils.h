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
