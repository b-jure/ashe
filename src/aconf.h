#ifndef ACONF_H
#define ACONF_H

#if defined(__linux__)
#include <linux/limits.h>
#define HOME	  "HOME"
#define PCS_EXTRA "/.-"
#else
#error "Ashe is only compatible with linux platforms."
#endif

/* ---- Prefix formats ----
 * Note: these currently do not support placeholders */
#define ASHE_INFO_PREFIX "[ashe ~ info]: "
#define ASHE_ERR_PREFIX	 "[ashe ~ error]: "

/* ---- Special environment variables ---- */
/* Note: you really shouldn't change these, if
 * they are regular alphanumeric values then you
 * will be able to overwrite them causing all sorts
 * of troubles for yourself. */
#define ASHE_VAR_STATUS "?"
#define ASHE_VAR_PID	"$"

/* ---- Reserved file descriptors ----
 * These are file descriptors ashe uses internally,
 * you can change them if you know what you are doing. */
#define ASHE_FD_0 10
#define ASHE_FD_1 11
#define ASHE_FD_2 12

/* ---- Placeholders ---- */
#define ASHE_PLH_SIGN '%'
typedef const char *(*ashe_promptfn)(void);
#ifdef ASHE_USE_PLACEHOLDERS_ARRAY /* include guard */
extern const char *ashe_host(void);
extern const char *ashe_user(void);
extern const char *ashe_jobc(void);
extern const char *ashe_dir(void);
extern const char *ashe_adir(void);
extern const char *ashe_time(void);
extern const char *ashe_date(void);
extern const char *ashe_uptime(void);
static ashe_promptfn placeholders[] = {
	ashe_host, /* 0: hostname */
	ashe_user, /* 1: username */
	ashe_jobc, /* 2: background jobs count */
	ashe_dir, /* 3: current directory (trimmed) */
	ashe_adir, /* 4: current directory (absolute path) */
	ashe_time, /* 5: current time (MM:HH) */
	ashe_date, /* 6: current date (DD:MM:YY) */
	ashe_uptime, /* 7: system uptime */
};
#endif

/* ---- Welcome message ---- */
#define ASHE_WELCOME      \
	"Welcome %1!\n"   \
	"\tuptime - %7\n" \
	"\ttime   - %5\n" \
	"\tdate   - %6\n"

/* ---- Prompt ---- */
#define ASHE_PROMPT "(%1)ashe$ "

/* ---- Settings ---- */
#define ASHE_SETTING_WARN_ON_EXIT 1
#define ASHE_SETTING_NOCLOBBER	  1

/* ---- Shell exit ---- */
/* Sleep time in-between shell sending a kill signal
 * to its child processes and trying to harvest them.
 * Time is in milliseconds and can be >=0 or up to 1000
 * milliseconds (1 second). */
#define ASHE_WAIT_BEFORE_HARVEST_MS 100

#endif
