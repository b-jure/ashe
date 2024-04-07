#include "acommon.h"
#include "aconf.h"
#include "autils.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

ASHE_PUBLIC void ashe_print(const char *msg, FILE *stream)
{
	fputs(msg, stream);
	fflush(stream);
	if (unlikely(ferror(stream)))
		panic(NULL);
}

ASHE_PUBLIC void ashe_printf(FILE *stream, const char *msg, ...)
{
	va_list argp;

	va_start(argp, msg);
	vfprintf(stream, msg, argp);
	fflush(stream);
	if (unlikely(ferror(stream))) {
		va_end(argp);
		panic(NULL); // can't print shit...
	}
	va_end(argp);
}

ASHE_PUBLIC void ashe_vprintf(FILE *stream, const char *msg, va_list argp)
{
	vfprintf(stream, msg, argp);
	fflush(stream);
	if (unlikely(ferror(stream))) {
		va_end(argp);
		panic(NULL); // can't print shit...
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

ASHE_PUBLIC void ashe_perrno(void)
{
	const char *errmsg;

	if (unlikely(errno == ENOMEM)) {
		perror(NULL);
	} else {
		errmsg = strerror(errno);
		ashe_eprintf(errmsg);
	}
}

ASHE_PUBLIC void printf_info(const char *ifmt, ...)
{
	va_list argp;

	va_start(argp, ifmt);
	ashe_vprintf_prefix(stderr, ifmt, ASHE_INFO_PREFIX, argp);
	va_end(argp);
}

ASHE_PUBLIC char *dupstrn(const char *str, memmax len)
{
	char *dup;

	dup = amalloc(len + 1);
	dup[len] = '\0';
	memcpy(dup, str, len);
	return dup;
}

ASHE_PUBLIC char *dupstr(const char *str)
{
	char *dup;
	memmax len;

	len = strlen(str) + 1;
	dup = amalloc(len);
	dup[len - 1] = '\0';
	memcpy(dup, str, len);
	return dup;
}

ASHE_PUBLIC memmax len_without_seq(const char *ptr)
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

ASHE_PUBLIC void expand_vars(a_arr_char *buffer)
{
	const char *value;
	char *ptr, *end;
	int32 klen, vlen;
	memmax offset;
	ssize diff;
	char cached;

	ptr = buffer->data;
	for (; ((ptr = strchr(ptr, '$')) != NULL); ptr++) {
		if (is_escaped(ptr, ptr - buffer->data))
			continue;
		offset = strspn(++ptr, ENV_VAR_CHARS);
		if (offset == 0 && (offset = is_ashe_var(ptr)) == 0)
			continue;
		end = ptr + offset;
		cached = *end;
		*end = '\0';
		value = getenv(ptr);
		*end = cached;
		--ptr; /* go back to '$' */
		klen = offset + 1; /* key chars + '$' */

		if (value != NULL) { /* found value ? */
			vlen = strlen(value);
			diff = vlen - klen;

			if (diff > 0) {
				if (likely(buffer->len + diff < ARG_MAX)) {
					a_arr_char_ensure(buffer, diff);
					memmove(end + diff, end, diff);
					buffer->len += diff;
				} else {
					goto l_remove;
				}
			} else {
				diff = -diff;
				memcpy(end - diff, end, diff);
				buffer->len -= diff;
			}

			memcpy(ptr, value, vlen);
		} else { /* no variable found, remove the whole key together with '$' */
l_remove:
			memcpy(ptr, end, klen);
			buffer->len -= klen;
		}
	}
}

ASHE_PUBLIC ubyte in_dq(char *str, memmax len)
{
	ubyte dq;

	dq = 0;
	while (len--)
		dq ^= (*str++ == '"');
	return dq;
}

ASHE_PUBLIC ubyte is_escaped(char *s, memmax curpos)
{
	char *at;

	at = s + curpos;
	return ((curpos > 1 && *(at - 1) == '\\' && *(at - 2) != '\\') ||
		(curpos == 1 && *(at - 1) == '\\'));
}

/* Unescape tabs, newlines, etc.. for more readable output in debug functions. */
ASHE_PUBLIC void unescape(a_arr_char *buffer, uint32 from, uint32 to)
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
ASHE_PUBLIC void escape(a_arr_char *buffer)
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
