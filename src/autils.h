#ifndef AUTILS_H
#define AUTILS_H

#include "atoken.h"
#include "acommon.h"

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

#define fd_inbounds(fd)	       ((fd) >= 0 && (fd) <= INT_MAX)
#define fd_isvalid(fd)	       (fcntl(fd, F_GETFD) != -1 || errno != EBADFD)
#define fd_isok(fd)	       (fd_inbounds(fd) && fd_isvalid(fd))
#define fd_isopen(fd, bitmask) ((bitmask) & fcntl(fd, F_GETFL))

/* open file for writing '>' or '>>' */
#define ashe_wopen(file, append) \
	open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)

/* open file for reading '<' */
#define ashe_ropen(file) open(file, O_RDONLY)

/* open file for reading and writing '<>' */
#define ashe_rwopen(file, append) \
	open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_RDWR, 0666)

/* generic print */
void ashe_print(const char *msg, FILE *stream);
void ashe_printf(FILE *stream, const char *msg, ...);
void ashe_vprintf(FILE *stream, const char *msg, va_list argp);

/* print error */
void ashe_eprintf(const char *errfmt, ...);
void ashe_perrno(void);

/* print info */
void printf_info(const char *ifmt, ...);

#ifdef ASHE_DBG
void ashe_dprintf(const char *dfmt, ...);
#else
#define ashe_dprintf(dftm, ...) ((void)(0))
#endif

/* duplicate string/bytes */
char *dupstr(const char *str);
char *dupstrn(const char *str, memmax len);

ubyte in_dq(const char *str, memmax len);
ubyte is_escaped(const char *s, memmax curpos);

/* Length without escape sequences */
memmax len_without_seq(const char *str);

/* char array processing */
void unescape(a_arr_char *buffer, uint32 from, uint32 to);
void escape(a_arr_char *buffer);
void expand_vars(a_arr_char *buffer);

/* syscall wrappers */
int32 ashe_close(int32 fd);
int32 ashe_dup2(int32 oldfd, int32 newfd);

#endif
