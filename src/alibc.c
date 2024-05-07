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

#include <errno.h>
#include <fcntl.h>

#include "acommon.h"
#include "ashell.h"
#include "alibc.h"
#include "autils.h"
#include "aalloc.h"

/*
 * libc wrappers
 */

ASHE_PUBLIC a_int32 ashe_open(const char *file, a_ubyte how, a_ubyte append)
{
	a_int32 fd;
	a_memmax o_flags;

	errno = 0;
	o_flags = 0;

	switch (how) {
	case AHOW_R:
		fd = open(file, O_RDONLY);
		break;
	case AHOW_RW:
		o_flags |= O_RDWR;
		goto checkappend;
	case AHOW_W:
		o_flags |= O_WRONLY;
checkappend:
		o_flags |= (append * O_APPEND) + (!append * O_TRUNC);
		fd = open(file, o_flags | O_CREAT, 0666);
		break;
	default:
		/* UNREACHED */
		ashe_panic_libwcall(ashe_open, "invalid open mode");
		break;
	}

	if (ASHE_UNLIKELY(fd < 0))
		ashe_perrno("can't open file '%s'", (file ? file : "(null)"));

	return fd;
}

ASHE_PUBLIC void ashe_dup2(a_int32 oldfd, a_int32 newfd)
{
	errno = 0;
	if (ASHE_UNLIKELY(dup2(oldfd, newfd) < 0))
		ashe_panic_libcall(dup2);
}

ASHE_PUBLIC void ashe_close(a_int32 fd)
{
	errno = 0;
	if (ASHE_UNLIKELY(close(fd) < 0))
		ashe_panic_libcall(close);
}

ASHE_PUBLIC void ashe_write(a_int32 fd, const void *buf, a_memmax bts)
{
	errno = 0;
	if (ASHE_UNLIKELY(write(fd, buf, bts) < 0))
		ashe_panic_libcall(write);
}

ASHE_PUBLIC a_ssize ashe_read(a_int32 fd, void *buf, a_memmax bts)
{
	a_ssize n;

	errno = 0;
	if (ASHE_UNLIKELY((n = read(fd, buf, bts)) < 0))
		ashe_panic_libcall(read);
	return n;
}

ASHE_PUBLIC void ashe_kill(a_pid pid, a_int32 sig)
{
	errno = 0;
	if (ASHE_UNLIKELY(kill(pid, sig) < 0))
		ashe_panic_libcall(kill);
}

ASHE_PUBLIC a_pid ashe_waitpid(a_pid pid, a_int32 *status, a_int32 opts)
{
	a_pid ret;

	errno = 0;
	if (ASHE_UNLIKELY((ret = waitpid(pid, status, opts)) < 0 && errno != ECHILD))
		ashe_panic_libcall(waitpid);
	return ret;
}

ASHE_PUBLIC void ashe_setpgid(a_pid pid, a_pid pgid)
{
	errno = 0;
	if (ASHE_UNLIKELY(setpgid(pid, pgid) < 0))
		ashe_panic_libcall(setpgid);
}

ASHE_PUBLIC void ashe_tcsetpgrp(a_pid pgid)
{
	errno = 0;
	if (ASHE_UNLIKELY(tcsetpgrp(STDIN_FILENO, pgid) < 0))
		ashe_panic_libcall(tcsetpgrp);
}

ASHE_PUBLIC void ashe_tcsetattr(a_int32 actions, const struct termios *tp)
{
	errno = 0;
	if (ASHE_UNLIKELY(tcsetattr(STDIN_FILENO, actions, tp) < 0))
		ashe_panic_libcall(tcsetattr);
}

ASHE_PUBLIC void ashe_tcgetattr(struct termios *tp)
{
	errno = 0;
	if (ASHE_UNLIKELY(tcgetattr(STDIN_FILENO, tp) < 0))
		ashe_panic_libcall(tcgetattr);
}

ASHE_PUBLIC void ashe_setenv(const char *name, const char *value, a_int32 overwrite)
{
	errno = 0;
	if (ASHE_UNLIKELY(setenv(name, value, overwrite) < 0))
		ashe_panic_libcall(setenv);
}

ASHE_PUBLIC void ashe_pipe(a_int32 *pipefds)
{
	errno = 0;
	if (ASHE_UNLIKELY(pipe(pipefds) < 0))
		ashe_panic_libcall(pipe);
}

ASHE_PUBLIC a_pid ashe_fork(void)
{
	a_pid pid;

	errno = 0;
	if (ASHE_UNLIKELY((pid = fork()) < 0))
		ashe_panic_libcall(fork);
	return pid;
}

ASHE_PUBLIC void ashe_sigaction(a_int32 sig, const struct sigaction *act, struct sigaction *oact)
{
	errno = 0;
	if (ASHE_UNLIKELY(sigaction(sig, act, oact) < 0))
		ashe_panic_libcall(sigaction);
}

ASHE_PUBLIC a_ssize ashe_snprintf(char *restrict s, a_memmax size, const char *restrict fmt, ...)
{
	va_list argp;
	a_ssize chars;

	va_start(argp, fmt);
	errno = 0;
	if (ASHE_UNLIKELY((chars = vsnprintf(s, size, fmt, argp)) < 0 || (a_memmax)chars > size)) {
		va_end(argp);
		ashe_panic_libcall(vsnprintf);
	}
	va_end(argp);
	return chars;
}

ASHE_PUBLIC a_pid ashe_getpgrp(void)
{
	a_pid pgid;

	if (ASHE_UNLIKELY((pgid = getpgrp()) < 0))
		ashe_panic_libcall(getpgrp);
	return pgid;
}

ASHE_PUBLIC ASHE_NORET ashe_exit(a_int32 status)
{
	if (ashe.sh_flags.isfork) {
		ashe_cleanupfork();
		_exit(status);
	}
	ashe_cleanup();
	exit(status);
}
