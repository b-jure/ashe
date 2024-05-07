#include <termios.h>
#include <sys/wait.h>

#include "acommon.h"

/*
 * libc wrappers
 * Use these when call to libc must not fail.
 * They invoke panic on error (except 'ashe_open').
 * Further, 'ashe_open' can still invoke panic, but
 * not if the 'open' itself errors, rather if invalid
 * open mode (AHOW) is passed (meaning skill issue).
 */

/* open */
#define AHOW_R	0
#define AHOW_W	2
#define AHOW_RW 4
a_int32 ashe_open(const char *file, a_ubyte how, a_ubyte append);

/* close */
void ashe_close(a_int32 fd);

/* dup2 */
void ashe_dup2(a_int32 oldfd, a_int32 newfd);

/* write */
void ashe_write(a_int32 fd, const void *buf, a_memmax bts);

/* read */
a_ssize ashe_read(a_int32 fd, void *buf, a_memmax bts);

/* kill */
void ashe_kill(a_pid pid, a_int32 sig);

/* waitpid */
a_pid ashe_waitpid(a_pid pid, a_int32 *status, a_int32 options);

/* setpgid */
void ashe_setpgid(a_pid pid, a_pid pgid);

/* tcsetpgrp */
void ashe_tcsetpgrp(a_pid pgid);

/* tcsetattr */
void ashe_tcsetattr(a_int32 actions, const struct termios *tp);

/* tcgetattr */
void ashe_tcgetattr(struct termios *tp);

/* setenv */
void ashe_setenv(const char *name, const char *value, a_int32 overwrite);

/* pipe */
void ashe_pipe(a_int32 *pipefds);

/* fork */
a_pid ashe_fork(void);

/* snprintf */
a_ssize ashe_snprintf(char *s, a_memmax size, const char *fmt, ...);

/* getpgrp */
a_pid ashe_getpgrp(void);

/* sigaction */
void ashe_sigaction(a_int32 sig, const struct sigaction *act, struct sigaction *oact);

/* exit */
ASHE_NORET ashe_exit(a_int32 status);
