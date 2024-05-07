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

#define ASHE_USE_PLACEHOLDERS_ARRAY
#include "acommon.h"
#include "atoken.h"
#include "auserstr.h"
#include "ajobcntl.h"
#include "ashell.h"
#include "autils.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <pwd.h>

static char plhbuf[BUFSIZ];

ASHE_PUBLIC const char *ashe_host(void)
{
	if (ASHE_UNLIKELY(gethostname(plhbuf, sizeof(plhbuf) - 1) < 0))
		ashe_panic_libcall(gethostname);
	return plhbuf;
}

ASHE_PUBLIC const char *ashe_user(void)
{
	struct passwd *record;
	uid_t uid;

	errno = 0;
	uid = getuid();
	if (ASHE_UNLIKELY(!(record = getpwuid(uid))))
		ashe_panic_libcall(getpwuid);
	ashe_snprintf(plhbuf, sizeof(plhbuf) - 1, "%s", record->pw_name);
	return plhbuf;
}

ASHE_PUBLIC const char *ashe_jobc(void)
{
	a_uint32 jobc = a_jobcntl_jobs(&ashe.sh_jobcntl);
	const char *fmt = (jobc ? "%u" : "");
	ashe_snprintf(plhbuf, sizeof(plhbuf) - 1, fmt, jobc);
	return plhbuf;
}

ASHE_PUBLIC const char *ashe_dir(void)
{
	const char *ptr;

	if (ASHE_UNLIKELY(!getcwd(plhbuf, BUFSIZ)))
		ashe_panic_libcall(getcwd);
	if ((ptr = strrchr(plhbuf, '/'))) {
		ptr++;
		ashe_snprintf(plhbuf, sizeof(plhbuf) - 1, "%s", ptr);
	}
	return plhbuf;
}

ASHE_PUBLIC const char *ashe_adir(void)
{
	if (ASHE_UNLIKELY(!getcwd(plhbuf, BUFSIZ)))
		ashe_panic_libcall(getcwd);
	return plhbuf;
}

ASHE_PUBLIC const char *ashe_time(void)
{
	time_t t;
	struct tm *lt;

	if (ASHE_UNLIKELY(time(&t) < 0))
		ashe_panic_libcall(time);
	if (ASHE_UNLIKELY((lt = localtime(&t)) == NULL))
		ashe_panic_libcall(localtime);
	ashe_snprintf(plhbuf, sizeof(plhbuf) - 1, "%02d:%02d", lt->tm_hour, lt->tm_min);
	return plhbuf;
}

ASHE_PUBLIC const char *ashe_date(void)
{
	struct tm *lt;
	time_t t;

	if (ASHE_UNLIKELY(time(&t) < 0))
		ashe_panic_libcall(time);
	if (ASHE_UNLIKELY((lt = localtime(&t)) == NULL))
		ashe_panic_libcall(localtime);
	ashe_snprintf(plhbuf, sizeof(plhbuf) - 1, "%d-%02d-%02d", lt->tm_year + 1900,
		      lt->tm_mon + 1, lt->tm_mday);
	return plhbuf;
}

ASHE_PUBLIC const char *ashe_uptime(void)
{
	struct sysinfo si;

	if (ASHE_UNLIKELY(sysinfo(&si) < 0))
		ashe_panic_libcall(sysinfo);
	ashe_snprintf(plhbuf, sizeof(plhbuf) - 1, "%ldh %ldm", (si.uptime / 3600),
		      (si.uptime / 60) % 60);
	return plhbuf;
}

ASHE_PRIVATE void expand_placeholders(a_arr_char *out, const char **ptr)
{
	const char *p = *ptr;
	const char *res;
	a_memmax n, prev;
	a_uint32 i;
	a_int32 c;

	if (!isdigit(*++p))
		goto push_plh_sign;

	n = prev = 0;
	for (i = 0; i < ASHE_MAXNUMSTR && isdigit((c = *p)); i++, p++) {
		n = n * 10 + (c - '0');
		if (ASHE_UNLIKELY(n < prev))
			ashe_panic("placeholder index overflowed");
		prev = n;
	}

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
ASHE_PUBLIC void parse_placeholders(a_arr_char *out, const char *str)
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
	a_arr_char_push(out, '\0');
}

/* Prints and parses any arbitrary string. */
ASHE_PUBLIC void ashe_puserstr(const char *str, a_memmax len)
{
	a_arr_char buffer;

	a_arr_char_init_cap(&buffer, len);
	parse_placeholders(&buffer, str);

	if (ASHE_UNLIKELY(a_arr_len(buffer) >= ASHE_USERSTR_MAX))
		a_arr_len(buffer) = ASHE_USERSTR_MAX - 1;

	ashe_print(a_arr_ptr(buffer), stderr);
	a_arr_char_free(&buffer, NULL);
}

/* Prints parsed welcome message. */
ASHE_PUBLIC void ashe_pwelcome(void)
{
	a_arr_len(ashe.sh_welcome) = 0;
	parse_placeholders(&ashe.sh_welcome, ASHE_WELCOME);
	ashe_print(a_arr_ptr(ashe.sh_welcome), stderr);
}
