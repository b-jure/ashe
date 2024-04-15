#ifndef ACONF_H
#define ACONF_H

/* Do not touch, unless you know what you are doing,
 * or are contributor... */
#if defined(__linux__)
#include <linux/limits.h>
#define HOME "HOME"
#else
#error "Ashe is only compatible with linux platforms."
#endif

/* ---- Asserts ---- */
#ifdef ASHE_DBG_ASSERT
#undef NDBG
#include <assert.h>
#ifndef ashe_assert
#define ashe_assert(expr) assert(expr)
#endif
#ifndef ashe_assertf
#define ashe_assertf(expr, msg) assert((expr) && (msg))
#endif
#else
#ifndef ashe_assert
#define ashe_assert(expr) (void)(0)
#endif
#ifndef ashe_assertf
#define ashe_assertf(expr, msg) (void)(0)
#endif
#endif /* ASHE_ASSERT */

/* ---- Prefix formats ----
 * Note: these currently do not support placeholders */
#define ASHE_DEBUG_PREFIX "[DEBUG]: "
#define ASHE_PANIC_PREFIX "[PANIC]: "
#define ASHE_INFO_PREFIX  "[INFO]: "
#define ASHE_ERR_PREFIX	  "[ERROR]: "

/* ---- Reserved file descriptors ----
 * These are file descriptors ashe uses internally,
 * you can change them if you know what you are doing. */
#define ASHE_FD_0 10
#define ASHE_FD_1 11
#define ASHE_FD_2 12

/* ---- Placeholders ---- */
#define ASHE_PLH_SIGN '%'
typedef const char *(*a_promptfn)(void);
#ifdef ASHE_USE_PLACEHOLDERS_ARRAY /* include guard */
extern const char *ashe_host(void);
extern const char *ashe_user(void);
extern const char *ashe_jobc(void);
extern const char *ashe_dir(void);
extern const char *ashe_adir(void);
extern const char *ashe_time(void);
extern const char *ashe_date(void);
extern const char *ashe_uptime(void);
static a_promptfn placeholders[] = {
	ashe_host, /* 0: hostname */
	ashe_user, /* 1: username */
	ashe_jobc, /* 2: background jobs count */
	ashe_dir, /* 3: current directory (trimmed) */
	ashe_adir, /* 4: current directory (absolute path) */
	ashe_time, /* 5: current time (HH:MM) */
	ashe_date, /* 6: current date (YYYY-MM-DD) */
	ashe_uptime, /* 7: system uptime (HHh MMm) */
};
#endif

/* ---- Welcome message ---- */
#define ASHE_WELCOME      \
	"Welcome %1!\n"   \
	"\tuptime - %7\n" \
	"\ttime   - %5\n" \
	"\tdate   - %6\n"

/* ---- Prompt ---- */
#define ASHE_PROMPT "[%4]$ "

/* ---- Settings ---- */
#define ASHE_SETTING_WARN_ON_EXIT 1
#define ASHE_SETTING_NOCLOBBER	  1

/* ---- Shell exit ---- */
/* Sleep time in-between shell sending a kill signal
 * to its child processes and trying to harvest them.
 * Time is in milliseconds and can be >=0 or up to 1000
 * milliseconds (1 second). */
#define ASHE_WAIT_BEFORE_HARVEST_MS 200

#endif
