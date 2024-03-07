#ifndef ACONF_H
#define ACONF_H

#if defined(__linux__)
#include <linux/limits.h>
#define HOME "HOME"
#define PCS_EXTRA "/.-"
#else
#error "Ashe is only compatible with linux platforms."
#endif


/* ---- Prefix formats ---- */
#define ASHE_ERR_PREFIX "[ashe ~ error]:"
#define ASHE_WARN_PREFIX "[ashe ~ warning]:"


/* Environment variable name where the status
 * code will be stored */
#define ASHE_STATUS_VAR "?"


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
    "ASHE shell\n" \
    "uptime - %uptime\n" \
    "time   - %time\n" \
    "date   - %date\n" \



/* ---- Prompt ---- */
#define ASHE_PROMPT_DEFAULT (ARG_MAX >> 4)
#define ASHE_PROMPT_LEN_MAX (ASHE_PROMPT_DEFAULT ? ASHE_PROMPT_DEFAULT : 1024)
#define ASHE_PROMPT \
    
#endif
