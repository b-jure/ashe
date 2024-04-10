#ifndef AUTILS_H
#define AUTILS_H

#include "atoken.h"
#include "acommon.h"

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

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
char *ashe_dupstrn(const char *str, a_memmax len);

a_ubyte ashe_indq(const char *str, a_memmax len);
a_ubyte ashe_isescaped(const char *s, a_memmax curpos);

/* Length without escape sequences */
a_memmax ashe_noescseq_len(const char *str);

/* buffer processing */
void ashe_unescape(a_arr_char *buffer, a_uint32 from, a_uint32 to);
void ashe_escape(a_arr_char *buffer);
void ashe_expandvars(a_arr_char *buffer);

/* syscall wrappers */
#define AHOW_R	0
#define AHOW_W	2
#define AHOW_RW 4
/* open wrapper */
a_int32 ashe_open(const char *file, a_ubyte how, a_ubyte append);
/* close wrapper */
a_int32 ashe_close(a_int32 fd);
/* dup2 wrapper */
a_int32 ashe_dup2(a_int32 oldfd, a_int32 newfd);
/* write wrapper */
a_int32 ashe_write(a_int32 fd, const void *buf, a_memmax bts);
/* exit wrapper */
void ashe_exit(a_int32 status);

#endif
