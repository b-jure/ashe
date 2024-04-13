#include "acommon.h"
#include "aconf.h"
#include "ashell.h"
#include "autils.h"

#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

ASHE_PRIVATE inline void ashe_flush(FILE *stream)
{
	fflush(stream);
	if (ASHE_UNLIKELY(ferror(stream)))
		ashe_panic(NULL);
}

ASHE_PUBLIC void ashe_print(const char *msg, FILE *stream)
{
	fputs(msg, stream);
	ashe_flush(stream);
}

ASHE_PUBLIC void ashe_printf(FILE *stream, const char *msg, ...)
{
	va_list argp;

	va_start(argp, msg);
	vfprintf(stream, msg, argp);
	ashe_flush(stream);
	va_end(argp);
}

ASHE_PUBLIC void ashe_vprintf(FILE *stream, const char *msg, va_list argp)
{
	vfprintf(stream, msg, argp);
	ashe_flush(stream);
}

ASHE_PRIVATE void ashe_vprintf_prefix(FILE *stream, const char *fmt, const char *prefix,
				      va_list argp)
{
	ashe_printf(stream, prefix);
	ashe_vprintf(stream, fmt, argp);
	ashe_print("\r\n", stream);
}

ASHE_PUBLIC void ashe_eprintf(const char *errfmt, ...)
{
	va_list argp;

	va_start(argp, errfmt);
	ashe_vprintf_prefix(stderr, errfmt, ASHE_ERR_PREFIX, argp);
	va_end(argp);
}

#ifdef ASHE_DBG
ASHE_PUBLIC void ashe_dprintf(const char *dfmt, ...)
{
	va_list argp;

	va_start(argp, dfmt);
	ashe_print("[DEBUG]: ", stderr);
	ashe_vprintf(stderr, dfmt, argp);
	va_end(argp);
	ashe_print("\r\n", stderr);
}
#endif

ASHE_PUBLIC void ashe_perrno(const char *errfmt, ...)
{
	va_list argp;

	ashe_assert(errno != 0);
	if (ASHE_UNLIKELY(errno == ENOMEM)) {
		perror("ashe");
		ashe_panic_abort();
	} else {
		ashe_print(ASHE_ERR_PREFIX, stderr);
		if (errfmt != NULL) {
			va_start(argp, errfmt);
			ashe_vprintf(stderr, errfmt, argp);
			va_end(argp);
			ashe_printf(stderr, ": %s\n\r", strerror(errno));
		} else {
			perror(NULL);
		}
	}
}

ASHE_PUBLIC void ashe_pinfo(const char *ifmt, ...)
{
	va_list argp;

	va_start(argp, ifmt);
	ashe_vprintf_prefix(stderr, ifmt, ASHE_INFO_PREFIX, argp);
	va_end(argp);
}

ASHE_PUBLIC char *ashe_dupstrn(const char *str, a_memmax len)
{
	char *dup;

	dup = ashe_malloc(len + 1);
	dup[len] = '\0';
	memcpy(dup, str, len);
	return dup;
}

ASHE_PUBLIC char *ashe_dupstr(const char *str)
{
	char *dup;
	a_memmax len;

	len = strlen(str) + 1;
	dup = ashe_malloc(len);
	dup[len - 1] = '\0';
	memcpy(dup, str, len);
	return dup;
}

ASHE_PUBLIC a_memmax ashe_noescseq_len(const char *ptr)
{
	a_memmax len, i;
	a_ubyte escape_seq;

	len = 0;
	escape_seq = 0;
	for (i = 0; ptr[i]; i++) {
		if (escape_seq) {
			if (ptr[i] == 'm')
				escape_seq = 0;
		} else if (ptr[i] == '\033') {
			escape_seq = 1;
		} else {
			len++;
		}
	}
	return len;
}

ASHE_PRIVATE a_memmax is_ashe_var(const char *ptr)
{
	a_memmax offset;

	offset = 0;
	switch (ptr[0]) {
	case ASHE_VAR_STATUS_C:
	case ASHE_VAR_PID_C:
		offset = 1;
		break;
	default:
		break;
	}
	return offset;
}

// clang-format off
ASHE_PUBLIC void ashe_expandvars(a_arr_char *buffer)
{
	const char *value;
	char *ptr, *end;
	a_ssize vlen, klen, diff, idx;
	a_byte cached;

	for (ptr = a_arrp_ptr(buffer); (ptr = strchr(ptr, '$')) != NULL; ptr++) {
		idx = ptr - a_arrp_ptr(buffer);

		if (ashe_isescaped(a_arrp_ptr(buffer), idx))
			continue;

		klen = strspn(++ptr, ENV_VAR_CHARS);

		if (klen == 0 && (klen = is_ashe_var(ptr)) == 0)
			continue;

		end = ptr + klen;
		cached = *end;
		*end = '\0';
		value = getenv(ptr);
		*end = cached;
		--ptr; /* back to '$' */
		klen++; /* + '$' */

		if (value) {
			vlen = strlen(value);
			diff = vlen - klen;
			if (ASHE_UNLIKELY(diff > 0 && a_arrp_len(buffer) + diff >= ARG_MAX))
				continue;
			a_arr_char_remove_str(buffer, idx, klen);
			a_arr_char_insert_str(buffer, idx, value, vlen);
			ptr = a_arrp_ptr(buffer) + idx + vlen - 1;
		} else { /* remove the key + '$' */
			--ptr; /* back to char before '$' */
			a_arr_char_remove_str(buffer, idx, klen);
		}
	}
}
// clang-format on

ASHE_PUBLIC a_ubyte ashe_indq(const char *restrict str, a_memmax len)
{
	a_ubyte dq;

	dq = 0;
	while (len--)
		dq ^= (*str++ == '"');
	return dq;
}

ASHE_PUBLIC a_ubyte ashe_isescaped(const char *restrict str, a_memmax curpos)
{
	const char *at;

	ashe_assert(str != NULL);
	at = str + curpos;
	return (curpos > 0 && ((curpos > 1 && at[-1] == '\\' && at[-2] != '\\') || at[-1] == '\\'));
}

/* Unescape tabs, newlines, etc.. for more readable output in debug functions. */
ASHE_PUBLIC void ashe_unescape(a_arr_char *buffer, a_uint32 from, a_uint32 to)
{
	static const a_int32 unescape[UINT8_MAX] = {
		['\a'] = 'a', ['\b'] = 'b', ['\f'] = 'f', ['\n'] = 'n',
		['\r'] = 'r', ['\t'] = 't', ['\v'] = 'v'
	};
	a_uint32 i;
	a_ubyte c;

	for (i = from; i < to; i++) {
		c = *a_arr_char_index(buffer, i);
		if (c == '\0')
			break;
		if (c == '\033') {
			*a_arr_char_index(buffer, i++) = '\\';
			a_arr_char_insert_str(buffer, i, "033", 3);
			to += 3;
			i += 2;
		} else if (unescape[c]) {
			*a_arr_char_index(buffer, i++) = '\\';
			a_arr_char_insert(buffer, i, unescape[c]);
			to++;
		}
	}
}

/* escape bytes that are outside of '"' */
ASHE_PUBLIC void ashe_escape(a_arr_char *buffer)
{
	static const a_int32 escape[UINT8_MAX] = {
		['a'] = '\a',  ['b'] = '\b', ['f'] = '\f', ['n'] = '\n',
		['r'] = '\r',  ['t'] = '\t', ['v'] = '\v', ['\\'] = '\\',
		['\''] = '\'', ['"'] = '\"', ['?'] = '\?', ['e'] = '\033',
	};
	char *oldp, *newp;
	a_ubyte dq;
	a_int32 c;

	oldp = a_arrp_ptr(buffer);
	newp = oldp;
	dq = 0;
	while (*oldp) {
		c = *(a_ubyte *)oldp++;
		if (c == '"') {
			dq ^= 1;
			continue;
		}
		if (c == '\\' && !dq) {
			c = *(a_ubyte *)oldp;
			if (c == '\0') {
				break;
			} else if (c == '0' && oldp[1] == '3' && oldp[2] == '3') {
				c = '\033';
				oldp += 3;
			} else if (escape[c]) {
				c = escape[c];
				oldp++;
			}
		}
		*newp++ = c;
	}
	*newp = '\0';
	a_arrp_len(buffer) = (newp - a_arrp_ptr(buffer)) + 1;
}

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

ASHE_PUBLIC ASHE_NORET ashe_exit(a_int32 status)
{
	if (ashe.sh_flags.isfork) {
		ashe_cleanupfork();
		_exit(status);
	}
	ashe_cleanup();
	exit(status);
}
