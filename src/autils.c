#include "autils.h"

/*
 * Allowed specifiers:
 * 	- %c (char)
 * 	- %% (%)
 * 	- %n (a_ssize)
 * 	- %f (double)
 * 	- %s (cstring)
 * 	- %p (void *)
 */
ASHE_PUBLIC void a_arr_char_push_strf(a_arr_char *buffer, const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	a_arr_char_push_vstrf(buffer, fmt, argp);
	va_end(argp);
}

/*
 * write implementation of this seperate from generic array
 * header ('aarray.h'), this is a bit too long to be inlined.
 */
ASHE_PUBLIC void a_arr_char_push_vstrf(a_arr_char *buffer, const char *fmt, va_list argp)
{
	a_ssize n;
	double f;
	a_ubyte c;
	const char *s, *end;
	const void *p;

	while ((end = strchr(fmt, '%')) != NULL) {
		a_arr_char_push_str(buffer, fmt, end - fmt);

		switch (*(end + 1)) {
		case 'c': /* char */
			c = (a_ubyte)va_arg(argp, int);
			a_arr_char_push(buffer, c);
			break;
		case 'n': /* a_ssize */
			n = va_arg(argp, a_ssize);
			a_arr_char_push_number(buffer, n);
			break;
		case 'f': /* double */
			f = va_arg(argp, double);
			a_arr_char_push_double(buffer, f);
			break;
		case 's': /* cstring */
			s = va_arg(argp, const char *);
			a_arr_char_push_str(buffer, s, strlen(s));
			break;
		case 'p': /* pointer */
			p = va_arg(argp, const void *);
			a_arr_char_push_ptr(buffer, p);
			break;
		case '%':
			a_arr_char_push(buffer, c);
			break;
		default:
			ashe_panicf("invalid format specifier '%%%c'", c);
			/* UNREACHED */
		}
		fmt = end + 2; /* % + specifier */
	}

	a_arr_char_push_str(buffer, fmt, strlen(fmt));
}

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
	a_arr_char buffer;

	a_arr_char_init(&buffer);
	a_arr_char_push_str(&buffer, prefix, strlen(prefix));
	a_arr_char_push_vstrf(&buffer, fmt, argp);
	a_arr_char_push_strlit(&buffer, "\r\n\0");
	ashe_print(a_arr_ptr(buffer), stream);

	a_arr_char_free(&buffer, NULL);
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
	ashe_vprintf_prefix(stderr, dfmt, ASHE_DEBUG_PREFIX, argp);
	va_end(argp);
}
#endif

ASHE_PUBLIC void ashe_perrno(const char *errfmt, ...)
{
	va_list argp;
	a_arr_char buffer;
	const char *error;

	ashe_assert(errno != 0);
	ashe_assert(errno != ENOMEM);

	error = strerror(errno);
	a_arr_char_init(&buffer);
	a_arr_char_push_strlit(&buffer, ASHE_ERR_PREFIX);
	if (errfmt != NULL) {
		va_start(argp, errfmt);
		a_arr_char_push_vstrf(&buffer, errfmt, argp);
		a_arr_char_push_strlit(&buffer, ": ");
		va_end(argp);
	}
	a_arr_char_push_strf(&buffer, "%s\r\n", error);
	a_arr_char_push(&buffer, '\0');
	ashe_print(a_arr_ptr(buffer), stderr);

	a_arr_char_free(&buffer, NULL);
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
			a_arr_char_remove_n(buffer, idx, klen);
			a_arr_char_insert_n(buffer, idx, value, vlen);
			ptr = a_arrp_ptr(buffer) + idx + vlen - 1;
		} else { /* remove the key + '$' */
			--ptr; /* back to char before '$' */
			a_arr_char_remove_n(buffer, idx, klen);
		}
	}
}

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
			a_arr_char_insert_n(buffer, i, "033", 3);
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
