#include "ashe_utils.h"
#include "input.h"

#include <stdarg.h>
#include <string.h>

#define TAB_SIZE 8

void die(void)
{
    perr();
    exit(EXIT_FAILURE);
}

void _die(void)
{
    perr();
    _exit(EXIT_FAILURE);
}

void pwarn(const byte* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, ASHE_WARN_PREFIX " ");
    vfprintf(stderr, fmt, argp);
    fprintf(stderr, "\r\n");
    fflush(stderr);
    va_end(argp);
}

void pinfo(info_t type, const byte* str)
{
    byte* info = NULL;

    switch(type) {
        case INF_NAME: info = "NAME"; break;
        case INF_USAGE: info = "SYNOPSIS"; break;
        case INF_DESC: info = "DESCRIPTION"; break;
    }

    /// HEADER
    fprintf(stderr, "\n\r" yellow(bold("%s")) "\n\r", info);
    /// PARAGRAPH
    fprintf(stderr, "%-s", str);

    fprintf(stderr, "\r\n");
    fflush(stderr);
}

void pmanpage(const byte* name, const byte* usage, const byte* desc)
{
    pinfo(INF_NAME, name);
    pinfo(INF_USAGE, usage);
    pinfo(INF_DESC, desc);
}

bool in_dq(byte* str, size_t len)
{
    bool dq = false;
    while(len--)
        if(*str++ == '"')
            dq ^= true;
    return dq;
}

bool is_escaped(byte* bt, size_t curpos)
{
    byte* at = bt + curpos;
    return ((curpos > 1 && *(at - 1) == '\\' && *(at - 2) != '\\') || (curpos == 1 && *(at - 1) == '\\'));
}

void perr(void)
{
    perror(ASHE_ERR_PREFIX);
}
