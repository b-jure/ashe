#include "ashe_utils.h"
#include "input.h"

#include <stdarg.h>

void die(void)
{
    perr();
    exit(EXIT_FAILURE);
}

void pwarn(const byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, ASHE_WARN_PREFIX " ");
    vfprintf(stderr, fmt, argp);
    fprintf(stderr, "\r\n");
    fflush(stderr);
    va_end(argp);
}

void pusage(const byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, green("Usage") ": ");
    vfprintf(stderr, fmt, argp);
    fprintf(stderr, "\r\n");
    fflush(stderr);
    va_end(argp);
}

void perr(void)
{
    perror(ASHE_ERR_PREFIX);
}
