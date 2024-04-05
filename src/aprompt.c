#define ASHE_USE_PLACEHOLDERS_ARRAY
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
	if (unlikely(gethostname(ashe_plhbuf, BUFSIZ) < 0))
		return NULL;
	return ashe_plhbuf;
}

ASHE_PUBLIC const char *ashe_user(void)
{
	struct passwd *record;
	uid_t uid;

	errno = 0;
	uid = getuid();
	if (unlikely(!(record = getpwuid(uid))))
		goto error;
	if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, "%s", record->pw_name) < 0))
		goto error;

	return ashe_plhbuf;
error:
	if (errno)
		ashe_perrno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_jobc(void)
{
	uint32 jobc = a_jobcntl_jobs(&ashe.sh_jobcntl);
	const char *fmt = (jobc ? "%u" : "");

	if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, fmt, jobc) < 0)) {
		ashe_perrno();
		return NULL;
	}
	return ashe_plhbuf;
}

ASHE_PUBLIC const char *ashe_dir(void)
{
	const char *ptr;

	if (unlikely(!getcwd(ashe_plhbuf, BUFSIZ)))
		goto error;
	if ((ptr = strrchr(ashe_plhbuf, '/'))) {
		ptr++;
		if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, "%s", ptr) < 0))
			goto error;
	}
	return ashe_plhbuf;
error:
	ashe_perrno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_adir(void)
{
	if (unlikely(!getcwd(ashe_plhbuf, BUFSIZ))) {
		ashe_perrno();
		return NULL;
	}
	return ashe_plhbuf;
}

ASHE_PUBLIC const char *ashe_time(void)
{
	struct tm *lt;
	time_t t;

	if (unlikely(time(&t) < 0 || (lt = localtime(&t)) == NULL))
		goto error;
	if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, "%02d:%02d", lt->tm_hour,
			      lt->tm_min) < 0))
		goto error;

	return ashe_plhbuf;
error:
	ashe_perrno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_date(void)
{
	struct tm *lt;
	time_t t;

	if (unlikely(time(&t) < 0 || (lt = localtime(&t)) == NULL))
		goto error;
	if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, "%d-%02d-%02d",
			      lt->tm_year + 1900, lt->tm_mon + 1,
			      lt->tm_mday) < 0))
		goto error;

	return ashe_plhbuf;
error:
	ashe_perrno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_uptime(void)
{
	struct sysinfo si;

	if (unlikely(sysinfo(&si) < 0 ||
		     snprintf(ashe_plhbuf, UINT_DIGITS, "%ldh %ldm",
			      (si.uptime / 3600), (si.uptime / 60) % 60) < 0)) {
		ashe_perrno();
		return NULL;
	}
	return ashe_plhbuf;
}

ASHE_PRIVATE void try_expand_placeholder(a_arr_char *out, const char **ptr)
{
	const char *p = *ptr;
	const char *res;
	memmax n = 0;
	uint32 i;
	int32 c;

	if (!isdigit(*++p))
		goto push_plh_sign;
	for (i = 0; i < UINT_DIGITS && isdigit((c = *p)); i++, p++)
		n = n * 10 + (c - '0');
	if (unlikely(n >= ELEMENTS(placeholders)))
		goto push_plh_sign;
	if (likely((res = placeholders[n]()) != NULL)) {
		a_arr_char_push_str(out, res, strlen(res));
		*ptr = p;
	} else {
push_plh_sign:
		a_arr_char_push(out, '%');
		*ptr += 1;
	}
}

/* Parses 'str' by expanding all placeholders. */
ASHE_PRIVATE void parsestring(a_arr_char *out, const char *str)
{
	int32 c;

	while ((c = *str)) {
		if (c != ASHE_PLH_SIGN) {
			a_arr_char_push(out, c);
			str++;
		} else {
			try_expand_placeholder(out, &str);
		}
	}
	out->len = ((ASHE_PROMPT_LEN_MAX - 1) *
		    (out->len >= ASHE_PROMPT_LEN_MAX)) +
		   (out->len * (out->len < ASHE_PROMPT_LEN_MAX));
	a_arr_char_push(out, '\0');
}

ASHE_PUBLIC void userstr_print(const char *str, memmax len)
{
	a_arr_char buffer;

	a_arr_char_init_cap(&buffer, len);
	parsestring(&buffer, str);
	ashe_print(buffer.data, stderr);
	a_arr_char_free(&buffer, NULL);
}

ASHE_PUBLIC void welcome_print(void)
{
	ashe.sh_welcome.len = 0;
	parsestring(&ashe.sh_welcome, ASHE_WELCOME);
	ashe_print(ashe.sh_welcome.data, stderr);
}

ASHE_PUBLIC void prompt_print(void)
{
	ashe.sh_prompt.len = 0;
	parsestring(&ashe.sh_prompt, ASHE_PROMPT);
	ashe.sh_term.tm_promptlen = ashe.sh_prompt.len - 1;
	ashe_print(ashe.sh_prompt.data, stderr);
}
