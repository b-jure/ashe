#ifndef AUTILS_H
#define AUTILS_H

#include "atoken.h"
#include "acommon.h"

#include <stdarg.h>
#include <stdio.h>

#define fdinbounds(fd)		((fd) >= 0 && (fd) <= INT_MAX)
#define fdisvalid(fd)		(fcntl(fd, F_GETFD) != -1 || errno != EBADFD)
#define fdisok(fd)		(fdinbounds(fd) && fdisvalid(fd))
#define fdisopened(fd, bitmask) ((bitmask) & fcntl(fd, F_GETFL))

/* generic print */
// void ashe_print(const char *msg);
void ashe_print(const char *msg, FILE *stream);
void ashe_printf(FILE *stream, const char *msg, ...);
void ashe_vprintf(FILE *stream, const char *msg, va_list argp);

/* print error */
void ashe_eprintf(const char *errfmt, ...);
void ashe_perrno(void);

/* print info */
void printf_info(const char *ifmt, ...);

/* duplicate string/bytes */
char *dupstr(const char *str);
char *dupstrn(const char *str, memmax len);

ubyte in_dq(char *str, memmax len);
ubyte is_escaped(char *s, memmax curpos);

/* Length without escape sequences */
memmax len_without_seq(const char *str);

/* Buffer processing */
void unescape(Buffer *buffer, uint32 from, uint32 to);
void escape(Buffer *buffer);
void expand_vars(Buffer *buffer);

#endif
