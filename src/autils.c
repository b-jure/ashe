#include "acommon.h"
#include "aconf.h"
#include "autils.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

ASHE_PUBLIC void vprintf_error_wprefix(const char *errfmt, va_list argp)
{
	fprintf(stderr, ASHE_ERR_PREFIX "");
	vfprintf(stderr, errfmt, argp);
	fputs("\r\n", stderr);
	fflush(stderr);
}

ASHE_PUBLIC void printf_error(const char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vprintf_error_wprefix(errfmt, argp);
	va_end(argp);
}

ASHE_PUBLIC void print_errno(void)
{
	ashe_assert(errno != 0, "invalid errno");
	const char *errmsg = strerror(errno);
	printf_error(errmsg); // unblock_signals() in here
}

ASHE_PUBLIC void vprintf_info_wprefix(const char *ifmt, va_list argp)
{
	fprintf(stderr, ASHE_INFO_PREFIX "");
	vfprintf(stderr, ifmt, argp);
	fputs("\r\n", stderr);
}

ASHE_PUBLIC void printf_info(const char *ifmt, ...)
{
	va_list argp;
	va_start(argp, ifmt);
	vprintf_info_wprefix(ifmt, argp);
	va_end(argp);
}

ASHE_PUBLIC char *dupstr(const char *str)
{
	char *dup = NULL;
	memmax len = strlen(str) + 1;
	dup = amalloc(len);
	memcpy(dup, str, len);
	return dup;
}

ASHE_PUBLIC memmax len_without_seq(const char *ptr)
{
	memmax len = 0;
	ubyte escape_seq = 0;
	for (memmax i = 0; ptr[i]; i++) {
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

ASHE_PUBLIC void expand_vars(Buffer *buffer)
{
	char *ptr = buffer->data;
	const char *value = NULL;

	for (; ((ptr = strchr(ptr, '$')) != NULL); ptr++) {
		if (is_escaped(ptr, ptr - buffer->data))
			continue;
		memmax offset = strspn(++ptr, ENV_VAR_CHARS);
		if (offset == 0)
			continue;
		char *end = ptr + offset;
		char cached = *end;
		*end = '\0';
		value = getenv(ptr);
		*end = cached;
		--ptr; // go back to '$'
		int32 klen = offset + 1; // key chars + '$'

		if (value != NULL) { // found value ?
			int32 vlen = strlen(value);
			ssize diff = vlen - klen;

			if (diff > 0) {
				if (likely(buffer->len + diff < ARG_MAX)) {
					Buffer_ensure(buffer, diff);
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
		} else { // no variable found, remove the whole key together with '$'
l_remove:
			memcpy(ptr, end, klen);
			buffer->len -= klen;
		}
	}
}

ASHE_PUBLIC ubyte in_dq(char *str, memmax len)
{
	ubyte dq = 0;
	while (len--)
		dq ^= (*str++ == '"');
	return dq;
}

ASHE_PUBLIC ubyte is_escaped(char *s, memmax curpos)
{
	char *at = s + curpos;
	return ((curpos > 1 && at[-1] == '\\' && at[-2] != '\\') ||
		(curpos == 1 && at[-1] == '\\'));
}

ASHE_PUBLIC void unescape(Buffer *buffer)
{
	static const int32 escape[UINT8_MAX] = {
		['a'] = '\a',  ['b'] = '\b', ['f'] = '\f', ['n'] = '\n',
		['r'] = '\r',  ['t'] = '\t', ['v'] = '\v', ['\\'] = '\\',
		['\''] = '\'', ['"'] = '\"', ['?'] = '\?', ['e'] = '\033',
	};

	char *oldp, *newp;
	oldp = newp = buffer->data;

	while (*oldp) {
		int32 c = *(unsigned char *)oldp++;
		if (c == '"')
			continue;
		if (c == '\\') {
			c = *(unsigned char *)oldp;
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
