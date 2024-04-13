#ifndef AUTILS_H
#define AUTILS_H

#include "atoken.h"
#include "acommon.h"

#include <termios.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

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

/*
 * libc wrappers
 * Use these when call to libc must not fail.
 * They invoke panic on error (except 'ashe_open').
 * Further, 'ashe_open' can still invoke panic, but
 * not if the 'open' itself errors, rather if invalid
 * open mode (AHOW) is passed (meaning skill issue).
 */

/* open wrapper */
#define AHOW_R	0
#define AHOW_W	2
#define AHOW_RW 4
a_int32 ashe_open(const char *file, a_ubyte how, a_ubyte append);
void ashe_close(a_int32 fd);
void ashe_dup2(a_int32 oldfd, a_int32 newfd);
void ashe_write(a_int32 fd, const void *buf, a_memmax bts);
a_ssize ashe_read(a_int32 fd, void *buf, a_memmax bts);
void ashe_kill(a_pid pid, a_int32 sig);
a_pid ashe_waitpid(a_pid pid, a_int32 *status, a_int32 options);
void ashe_setpgid(a_pid pid, a_pid pgid);
void ashe_tcsetpgrp(a_pid pgid);
void ashe_tcsetattr(a_int32 actions, const struct termios *tp);
void ashe_tcgetattr(struct termios *tp);
void ashe_setenv(const char *name, const char *value, a_int32 overwrite);
void ashe_pipe(a_int32 *pipefds);
a_pid ashe_fork(void);
void ashe_sigaction(a_int32 sig, const struct sigaction *act, struct sigaction *oact);
ASHE_NORET ashe_exit(a_int32 status);

#endif
