#include "ashe_utils.h"

#include <stdarg.h>

void pwarn(const byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, ASHE_WARN_PREFIX);
    vprintf(fmt, argp);
    fprintf(stderr, "\n");
    va_end(argp);
}

void pusage(const byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, "Usage: ");
    vprintf(fmt, argp);
    fprintf(stderr, "\n");
    va_end(argp);
}

void perr(void)
{
    perror(ASHE_ERR_PREFIX);
}
