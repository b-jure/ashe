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

void pinfo(info_t type, const byte *fmt, ...)
{
    byte *info = NULL;

    switch(type) {
        case INF_NAME: info = "NAME"; break;
        case INF_USAGE: info = "SYNOPSIS"; break;
        case INF_DESC: info = "DESCRIPTION"; break;
    }

    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, "\n\r" yellow(bold("%s")) "\n\r\t", info);
    vfprintf(stderr, fmt, argp);
    fprintf(stderr, "\r\n");
    fflush(stderr);
    va_end(argp);
}

void pmanpage(const byte *name, const byte *usage, const byte *desc)
{
    pinfo(INF_NAME, "%s", name);
    pinfo(INF_USAGE, "%s", usage);
    pinfo(INF_DESC, "%s", desc);
}

void perr(void)
{
    perror(ASHE_ERR_PREFIX);
}
