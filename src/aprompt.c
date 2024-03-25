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
		print_errno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_jobc(void)
{
	uint32 jobc = JobControl_jobs(&ashe.sh_jobcntl);
	const char *fmt = (jobc ? "%u" : "");

	if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, fmt, jobc) < 0)) {
		print_errno();
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
	print_errno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_adir(void)
{
	if (unlikely(!getcwd(ashe_plhbuf, BUFSIZ))) {
		print_errno();
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
	if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, "%02d:%02d", lt->tm_min,
			      lt->tm_hour)))
		goto error;

	return ashe_plhbuf;
error:
	print_errno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_date(void)
{
	struct tm *lt;
	time_t t;

	if (unlikely(time(&t) < 0 || (lt = localtime(&t)) == NULL))
		goto error;
	if (unlikely(snprintf(ashe_plhbuf, BUFSIZ, "%02d:%02d:%d", lt->tm_mday,
			      lt->tm_mon, lt->tm_year + 1900) < 0))
		goto error;

	return ashe_plhbuf;
error:
	print_errno();
	return NULL;
}

ASHE_PUBLIC const char *ashe_uptime(void)
{
	struct sysinfo si;

	if (unlikely(sysinfo(&si) < 0 ||
		     snprintf(ashe_plhbuf, UINT_DIGITS, "%ldm:%ldh",
			      (si.uptime / 60) % 60, (si.uptime / 3600)) < 0)) {
		print_errno();
		return NULL;
	}
	return ashe_plhbuf;
}

ASHE_PRIVATE void try_expand_placeholder(Buffer *out, const char **ptr)
{
	const char *p = *++ptr;
	const char *res;
	memmax n = 0;
	uint32 i;
	int32 c;

	if (!isdigit(*p))
		return;
	for (i = 0; i < UINT_DIGITS && isdigit((c = *p)); i++) {
		n = n * 10 + (c - '0');
		p++;
	}
	if (unlikely(n >= ELEMENTS(placeholders)))
		return;
	if (likely((res = placeholders[n]()) != NULL)) {
		Buffer_push_str(out, res, strlen(res));
		*ptr = p;
	} else {
		Buffer_push(out, '%');
	}
}

/* Parses 'str' by expanding all placeholders. */
ASHE_PRIVATE void parsestring(Buffer *out, const char *str)
{
	int32 c;

	while ((c = *str)) {
		if (c != ASHE_PLH_SIGN) {
			Buffer_push(out, c);
			str++;
		} else {
			try_expand_placeholder(out, &str);
		}
	}
	out->len = (ASHE_PROMPT_LEN_MAX * (out->len >= ASHE_PROMPT_LEN_MAX)) +
		   (out->len * (out->len < ASHE_PROMPT_LEN_MAX));
	Buffer_push(out, '\0');
}

ASHE_PUBLIC void print_userstr(const char *str, memmax len, uint32 bufidx)
{
	static Buffer buff;

	if (unlikely(ashe.sh_buffers.len < 1)) { /* executes only once */
		Buffer_init_cap(&buff, len);
		ArrayCharptr_insert(&ashe.sh_buffers, bufidx, buff.data);
	}
	parsestring(&buff, str);
	ashe.sh_buffers.data[bufidx] = buff.data;
	if (likely(bufidx == 1))
		ashe.sh_term.tm_promptlen = buff.len - 1;
	fputs(buff.data, stderr);
	fflush(stderr);
	if (unlikely(ferror(stderr))) {
		print_errno();
		panic(NULL);
	}
}
