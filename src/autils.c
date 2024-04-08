#include "acommon.h"
#include "aconf.h"
#include "ashell.h"
#include "autils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

ASHE_PUBLIC void ashe_print(const char *msg, FILE *stream)
{
	fputs(msg, stream);
	fflush(stream);
	if (unlikely(ferror(stream)))
		ashe_panic(NULL);
}

ASHE_PUBLIC void ashe_printf(FILE *stream, const char *msg, ...)
{
	va_list argp;

	va_start(argp, msg);
	vfprintf(stream, msg, argp);
	fflush(stream);
	if (unlikely(ferror(stream))) {
		va_end(argp);
		ashe_panic(NULL); // can't print shit...
	}
	va_end(argp);
}

ASHE_PUBLIC void ashe_vprintf(FILE *stream, const char *msg, va_list argp)
{
	vfprintf(stream, msg, argp);
	fflush(stream);
	if (unlikely(ferror(stream))) {
		va_end(argp);
		ashe_panic(NULL); // can't print shit...
	}
}

ASHE_PRIVATE void ashe_vprintf_prefix(FILE *stream, const char *fmt,
				      const char *prefix, va_list argp)
{
	ashe_printf(stream, prefix); /* memleak 'va_list' on panic */
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
	if (unlikely(errno == ENOMEM)) {
		perror(__func__);
	} else {
		ashe_print(ASHE_ERR_PREFIX, stderr);
		if (errfmt != NULL) {
			va_start(argp, errfmt);
			ashe_vprintf(stderr, errfmt, argp);
			va_end(argp);
			ashe_printf(stderr, ": %s\n\r", strerror(errno));
		} else {
			perror(__func__);
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

ASHE_PUBLIC char *ashe_dupstrn(const char *str, memmax len)
{
	char *dup;

	dup = amalloc(len + 1);
	dup[len] = '\0';
	memcpy(dup, str, len);
	return dup;
}

ASHE_PUBLIC char *ashe_dupstr(const char *str)
{
	char *dup;
	memmax len;

	len = strlen(str) + 1;
	dup = amalloc(len);
	dup[len - 1] = '\0';
	memcpy(dup, str, len);
	return dup;
}

ASHE_PUBLIC memmax ashe_noescseq_len(const char *ptr)
{
	memmax len, i;
	ubyte escape_seq;

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

ASHE_PRIVATE memmax is_ashe_var(const char *ptr)
{
	memmax offset;

	offset = 0;
	switch (ptr[0]) {
	case ASHE_VAR_STATUS_C:
	case ASHE_VAR_PID_C:
		if (isspace(ptr[1]) || ptr[1] == '\0')
			offset = 1;
		break;
	default:
		break;
	}
	return offset;
}

ASHE_PUBLIC void ashe_expandvars(a_arr_char *buffer)
{
	const char *value;
	char *ptr, *end;
	ssize vlen, klen, diff, idx;
	byte cached;

	for (ptr = buffer->data; (ptr = strchr(ptr, '$')) != NULL; ptr++) {
		idx = ptr - buffer->data;

		if (ashe_isescaped(buffer->data, idx))
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
			if (unlikely(diff > 0 && buffer->len + diff >= ARG_MAX))
				continue;
			a_arr_char_remove_str(buffer, idx, klen);
			a_arr_char_insert_str(buffer, idx, value, vlen);
			ptr = buffer->data + idx + vlen - 1;
		} else { /* remove the key + '$' */
			--ptr; /* back to char before '$' */
			a_arr_char_remove_str(buffer, idx, klen);
		}
	}
}

ASHE_PUBLIC ubyte ashe_indq(const char *restrict str, memmax len)
{
	ubyte dq;

	dq = 0;
	while (len--)
		dq ^= (*str++ == '"');
	return dq;
}

ASHE_PUBLIC ubyte ashe_isescaped(const char *restrict str, memmax curpos)
{
	const char *at;

	ashe_assert(str != NULL);
	at = str + curpos;
	return (curpos > 0 &&
		((curpos > 1 && at[-1] == '\\' && at[-2] != '\\') ||
		 at[-1] == '\\'));
}

/* Unescape tabs, newlines, etc.. for more readable output in debug functions. */
ASHE_PUBLIC void ashe_unescape(a_arr_char *buffer, uint32 from, uint32 to)
{
	static const int32 unescape[UINT8_MAX] = {
		['\a'] = 'a', ['\b'] = 'b', ['\f'] = 'f', ['\n'] = 'n',
		['\r'] = 'r', ['\t'] = 't', ['\v'] = 'v'
	};
	uint32 i;
	ubyte c;

	for (i = from; i < to; i++) {
		c = *a_arr_char_index(buffer, i);
		if (c == '\0')
			break;
		if (c == '\033') {
			*a_arr_char_index(buffer, i++) = '\\';
			a_arr_char_insert(buffer, i++, '0');
			a_arr_char_insert(buffer, i++, '3');
			a_arr_char_insert(buffer, i, '3');
			to += 3;
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
	static const int32 escape[UINT8_MAX] = {
		['a'] = '\a',  ['b'] = '\b', ['f'] = '\f', ['n'] = '\n',
		['r'] = '\r',  ['t'] = '\t', ['v'] = '\v', ['\\'] = '\\',
		['\''] = '\'', ['"'] = '\"', ['?'] = '\?', ['e'] = '\033',
	};
	char *oldp, *newp;
	ubyte dq;
	int32 c;

	oldp = buffer->data;
	newp = oldp;
	dq = 0;
	while (*oldp) {
		c = *(ubyte *)oldp++;
		if (c == '"') {
			dq ^= 1;
			continue;
		}
		if (c == '\\' && !dq) {
			c = *(ubyte *)oldp;
			if (c == '\0') {
				break;
			} else if (c == '0' && oldp[1] == '3' &&
				   oldp[2] == '3') {
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
	buffer->len = (newp - buffer->data) + 1;
}

/*
 * SYSCALL WRAPPERS
 */

ASHE_PUBLIC int32 ashe_open(const char *file, memmax how, ...)
{
	va_list ap;
	ubyte append;
	int32 fd;
	memmax o_flags;

	o_flags = 0;

	switch (how) {
	case AHOW_R:
		fd = open(file, O_RDONLY);
		break;
	case AHOW_RW:
		o_flags |= O_RDWR;
		goto checkappend;
	case AHOW_W:
		va_start(ap, how);
		o_flags |= O_WRONLY;
checkappend:
		append = (ubyte)va_arg(ap, uint32);
		o_flags |= (append * O_APPEND) + (!append * O_TRUNC);
		fd = open(file, o_flags | O_CREAT, 0666);
		va_end(ap);
		break;
	default:
		/* UNREACHED */
		ashe_panic("invalid open mode");
		break;
	}

	if (unlikely(fd < 0))
		ashe_perrno("can't open file '%s'", (file ? file : "(null)"));
	return fd;
}

ASHE_PUBLIC int32 ashe_dup2(int32 oldfd, int32 newfd)
{
	if (unlikely(dup2(oldfd, newfd) < 0)) {
		ashe_perrno("ashe_dup2(%d, %d) failed", oldfd, newfd);
		return -1;
	}
	return 0;
}

ASHE_PUBLIC int32 ashe_close(int32 fd)
{
	if (unlikely(close(fd) < 0)) {
		ashe_perrno("ashe_close(%d) failed", fd);
		return -1;
	}
	return 0;
}

ASHE_PUBLIC int32 ashe_write(int32 fd, const void *buf, memmax bts)
{
	if (unlikely(write(fd, buf, bts) < 0)) {
		ashe_perrno("failed writting %zu bytes to fd %d from %p", bts,
			    fd, buf);
		return -1;
	}
	return 0;
}

ASHE_PUBLIC void ashe_exit(int32 status)
{
	if (ashe.sh_flags.isfork) {
		ashe_cleanupfork();
		_exit(status);
	}
	ashe_cleanup();
	exit(status);
}
