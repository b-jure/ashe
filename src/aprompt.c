#define ASHE_USE_PLACEHOLDERS_ARRAY
#include "acommon.h"
#include "atoken.h"
#include "aprompt.h"
#include "ajobcntl.h"
#include "ashell.h"
#include "autils.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <pwd.h>

#define ASHE_PROMPT_LEN_MAX ((ARG_MAX >> 4) ? (ARG_MAX >> 4) : 1024)

static char ashe_plhbuf[BUFSIZ];

ASHE_PUBLIC const char *ashe_host(void)
{
	if (ASHE_UNLIKELY(gethostname(ashe_plhbuf, BUFSIZ) < 0))
		return NULL;
	return ashe_plhbuf;
}

ASHE_PUBLIC const char *ashe_user(void)
{
	struct passwd *record;
	uid_t uid;

	errno = 0;
	uid = getuid();
	if (ASHE_UNLIKELY(!(record = getpwuid(uid)))) {
		ashe_perrno("getpwuid");
		goto error;
	}
	if (ASHE_UNLIKELY(snprintf(ashe_plhbuf, BUFSIZ, "%s", record->pw_name) < 0)) {
		ashe_perrno("snprintf");
		goto error;
	}

	return ashe_plhbuf;
error:
	return NULL;
}

ASHE_PUBLIC const char *ashe_jobc(void)
{
	a_uint32 jobc = a_jobcntl_jobs(&ashe.sh_jobcntl);
	const char *fmt = (jobc ? "%u" : "");

	if (ASHE_UNLIKELY(snprintf(ashe_plhbuf, BUFSIZ, fmt, jobc) < 0)) {
		ashe_perrno("snprintf");
		return NULL;
	}
	return ashe_plhbuf;
}

ASHE_PUBLIC const char *ashe_dir(void)
{
	const char *ptr;

	if (ASHE_UNLIKELY(!getcwd(ashe_plhbuf, BUFSIZ))) {
		ashe_perrno("getcwd");
		goto error;
	}
	if ((ptr = strrchr(ashe_plhbuf, '/'))) {
		ptr++;
		if (ASHE_UNLIKELY(snprintf(ashe_plhbuf, BUFSIZ, "%s", ptr) < 0)) {
			ashe_perrno("snprintf");
			goto error;
		}
	}
	return ashe_plhbuf;
error:
	return NULL;
}

ASHE_PUBLIC const char *ashe_adir(void)
{
	if (ASHE_UNLIKELY(!getcwd(ashe_plhbuf, BUFSIZ))) {
		ashe_perrno("getcwd");
		return NULL;
	}
	return ashe_plhbuf;
}

ASHE_PUBLIC const char *ashe_time(void)
{
	time_t t;
	struct tm *lt;

	if (ASHE_UNLIKELY(time(&t) < 0)) {
		ashe_perrno("time");
		goto error;
	}
	if (ASHE_UNLIKELY((lt = localtime(&t)) == NULL)) {
		ashe_perrno("localtime");
		goto error;
	}
	if (ASHE_UNLIKELY(snprintf(ashe_plhbuf, BUFSIZ, "%02d:%02d", lt->tm_hour, lt->tm_min) <
			  0)) {
		ashe_perrno("snprintf");
		goto error;
	}

	return ashe_plhbuf;
error:
	return NULL;
}

ASHE_PUBLIC const char *ashe_date(void)
{
	struct tm *lt;
	time_t t;

	if (ASHE_UNLIKELY(time(&t) < 0)) {
		ashe_perrno("time");
		goto error;
	}
	if (ASHE_UNLIKELY((lt = localtime(&t)) == NULL)) {
		ashe_perrno("localtime");
		goto error;
	}
	if (ASHE_UNLIKELY(snprintf(ashe_plhbuf, BUFSIZ, "%d-%02d-%02d", lt->tm_year + 1900,
				   lt->tm_mon + 1, lt->tm_mday) < 0)) {
		ashe_perrno("snprintf");
		goto error;
	}

	return ashe_plhbuf;
error:
	return NULL;
}

ASHE_PUBLIC const char *ashe_uptime(void)
{
	struct sysinfo si;

	if (ASHE_UNLIKELY(sysinfo(&si) < 0)) {
		ashe_perrno("sysinfo");
		goto error;
	}
	if (ASHE_UNLIKELY(snprintf(ashe_plhbuf, ASHE_INT64_DIGITS, "%ldh %ldm", (si.uptime / 3600),
				   (si.uptime / 60) % 60) < 0)) {
		ashe_perrno("snprintf");
		goto error;
	}
	return ashe_plhbuf;
error:
	return NULL;
}

ASHE_PRIVATE void expand_placeholders(a_arr_char *out, const char **ptr)
{
	const char *p = *ptr;
	const char *res;
	a_memmax n = 0;
	a_uint32 i;
	a_int32 c;

	if (!isdigit(*++p))
		goto push_plh_sign;
	for (i = 0; i < ASHE_INT64_DIGITS && isdigit((c = *p)); i++, p++)
		n = n * 10 + (c - '0');
	if (ASHE_UNLIKELY(n >= ASHE_ELEMENTS(placeholders)))
		goto push_plh_sign;
	if (ASHE_LIKELY((res = placeholders[n]()) != NULL)) {
		a_arr_char_push_str(out, res, strlen(res));
		*ptr = p;
	} else {
push_plh_sign:
		a_arr_char_push(out, '%');
		*ptr += 1;
	}
}

/* parses 'str' by expanding all placeholders */
ASHE_PRIVATE void parse_placeholders(a_arr_char *out, const char *str)
{
	a_int32 c;

	while ((c = *str)) {
		if (c != ASHE_PLH_SIGN) {
			a_arr_char_push(out, c);
			str++;
		} else {
			expand_placeholders(out, &str);
		}
	}

	if (ASHE_UNLIKELY(a_arrp_len(out) >= ASHE_PROMPT_LEN_MAX))
		a_arrp_len(out) = ASHE_PROMPT_LEN_MAX - 1;

	a_arr_char_push(out, '\0');
}

ASHE_PUBLIC void userstr_print(const char *str, a_memmax len)
{
	a_arr_char buffer;

	a_arr_char_init_cap(&buffer, len);
	parse_placeholders(&buffer, str);
	ashe_print(a_arr_ptr(buffer), stderr);
	a_arr_char_free(&buffer, NULL);
}

ASHE_PUBLIC void ashe_pwelcome(void)
{
	a_arr_len(ashe.sh_welcome) = 0;
	parse_placeholders(&ashe.sh_welcome, ASHE_WELCOME);
	ashe_print(a_arr_ptr(ashe.sh_welcome), stderr);
}

ASHE_PUBLIC void ashe_pprompt(void)
{
	a_arr_len(ashe.sh_prompt) = 0;
	parse_placeholders(&ashe.sh_prompt, ASHE_PROMPT);
	ashe.sh_term.tm_promptlen = a_arr_len(ashe.sh_prompt) - 1;
	ashe_print(a_arr_ptr(ashe.sh_prompt), stderr);
}
