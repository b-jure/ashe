#ifndef ACONF_H
#define ACONF_H


#if defined(__linux__)
#include <linux/limits.h>
#define HOME "HOME"
#define PCS_EXTRA "/.-"
#else
#error "Ashe is only compatible with linux platforms."
#endif




/* ---- Prefix formats ----
 * Note: these currently do not support placeholders */
#define ASHE_INFO_PREFIX "[ashe ~ info]: "
#define ASHE_ERR_PREFIX "[ashe ~ error]: "




/* ---- Special environment variables ---- */
#define ASHE_STATUS_VAR "?"
#define ASHE_PID "$"



/* ---- Reserved file descriptors ----
 * These are file descriptors ashe uses internally,
 * you can change them if you know what you are doing. */
#define ASHE_FD_0 10
#define ASHE_FD_1 11
#define ASHE_FD_2 12



/* ---- Placeholders ---- */
#define PP_HOST "%host" // host name
#define PP_USER "%user" // user name
#define PP_JOBC "%jobc" // background jobs count
#define PP_DIR "%dir" // current directory
#define PP_ADIR "%adir" // current directory absolute path
#define PP_NDIR(n) "%" #n "dir" // current directory trimmed to 'n' length
#define PP_TIME "%time" // time in format - '19:15' (HH:MM)
#define PP_DATE "%date" // date in format - '15.05.2024' (DD.MM.YY)
#define PP_UPTIME "%uptime" // system uptime in format - '8:53' (H[H]:M[M])




/* ---- Welcome message ---- */
#define ASHE_WELCOME \
    "Welcome %user!\n" \
    "\tuptime - %uptime\n" \
    "\ttime   - %time\n" \
    "\tdate   - %date\n" \




/* ---- Prompt ---- */
#define ASHE_PROMPT "ashe $ "




/* ---- Settings ---- */
#define ASHE_SETTING_WARN_ON_EXIT 1
#define ASHE_SETTING_NOCLOBBER 1



/* ---- Shell exit ---- */
/* Sleep time in-between shell sending a kill signal
 * to its child processes and trying to harvest them.
 * Time is in milliseconds and can be >=0 or up to 1000
 * milliseconds (1 second). */
#define ASHE_WAIT_BEFORE_HARVEST_MS 100


#endif
