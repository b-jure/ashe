#ifndef AUTILS_H
#define AUTILS_H

#include "atoken.h"
#include "acommon.h"

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

#define a_fd_inbounds(fd)	 ((fd) >= 0 && (fd) <= INT_MAX)
#define a_fd_isvalid(fd)	 (fcntl(fd, F_GETFD) != -1 || errno != EBADFD)
#define a_fd_isok(fd)		 (a_fd_inbounds(fd) && a_fd_isvalid(fd))
#define a_fd_isopen(fd, bitmask) ((bitmask) & fcntl(fd, F_GETFL))

/* generic print */
void ashe_print(const char *msg, FILE *stream);
void ashe_printf(FILE *stream, const char *msg, ...);
void ashe_vprintf(FILE *stream, const char *msg, va_list argp);

/* print error */
void ashe_eprintf(const char *errfmt, ...);
void ashe_perrno(const char *errfmt, ...);

/* print info */
void ashe_pinfo(const char *ifmt, ...);

#ifdef ASHE_DBG
void ashe_dprintf(const char *dfmt, ...);
#else
#define ashe_dprintf(dftm, ...) ((void)(0))
#endif

/* duplicate string/bytes */
char *ashe_dupstr(const char *str);
char *ashe_dupstrn(const char *str, memmax len);

ubyte ashe_indq(const char *str, memmax len);
ubyte ashe_isescaped(const char *s, memmax curpos);

/* Length without escape sequences */
memmax ashe_noescseq_len(const char *str);

/* buffer processing */
void ashe_unescape(a_arr_char *buffer, uint32 from, uint32 to);
void ashe_escape(a_arr_char *buffer);
void ashe_expandvars(a_arr_char *buffer);

/* syscall wrappers */
#define AHOW_R	0
#define AHOW_W	2
#define AHOW_RW 4
/* open wrapper */
int32 ashe_open(const char *file, memmax how, ...);
/* close wrapper */
int32 ashe_close(int32 fd);
/* dup2 wrapper */
int32 ashe_dup2(int32 oldfd, int32 newfd);
/* write wrapper */
int32 ashe_write(int32 fd, const void *buf, memmax bts);
/* exit wrapper */
void ashe_exit(int32 status);

#endif
