#include "ashe_utils.h"

#include <stdarg.h>

void pprompt(void)
{
    fprintf(stderr, "%s", PROMPT);
    fflush(stderr);
}

void pwarn(const byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, ASHE_WARN_PREFIX);
    vfprintf(stderr, fmt, argp);
    fprintf(stderr, "\n");
    va_end(argp);
}

void pusage(const byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, "Usage: ");
    vfprintf(stderr, fmt, argp);
    fprintf(stderr, "\n");
    va_end(argp);
}

void perr(void)
{
    perror(ASHE_ERR_PREFIX);
}
