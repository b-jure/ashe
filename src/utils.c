#include "ashe_utils.h"
#include "input.h"
#include "shell.h"

#include <assert.h>
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

    size_t len = strlen(str);
    byte   str_mut[len + 1];
    memcpy(str_mut, str, len + 1);

    uint16_t col_limit = terminal.tm_columns - 10;

    size_t cap = len + (len / col_limit) + 2;
    byte   buff[cap]; /* 2 for good measure LOOOL ??? */
    byte*  target = buff;
    buff[0]       = '\0';
    memset(buff, 0, cap);

    /// HEADER
    fprintf(stderr, "\n" yellow(bold("%s")) "\n\r", info);

    uint16_t    remaining_space = col_limit;
    const byte* delimiter       = " ";
    const byte* word            = strtok(str_mut, delimiter);
    byte*       ptr             = NULL;

    while(is_some(word)) {
        size_t word_len = len_without_seq(word);

        if(remaining_space > word_len) {
            target += sprintf(target, "%s ", word);
            remaining_space -= (word_len + 1); // Account for delimiter (1)
        } else {
            target += sprintf(target, "\n%s ", word);
            remaining_space = col_limit - word_len - 1; // Account for delimiter (1)
        }

        if(is_some((ptr = strchr(word, '\n')))) {
            remaining_space = col_limit - strlen(ptr + 1);
        }

        word = strtok(NULL, delimiter);
    }

    fprintf(stderr, "\n" yellow(bold("%s")) "\n%-s\n", info, buff);
    fflush(stderr);
}

void pmanpage(const byte* name, const byte* usage, const byte* desc)
{
    pinfo(INF_NAME, name);
    pinfo(INF_USAGE, usage);
    pinfo(INF_DESC, desc);
}

size_t len_without_seq(const byte* ptr)
{
    size_t len        = 0;
    bool   escape_seq = false;

    for(size_t i = 0; ptr[i]; i++) {
        if(escape_seq) {
            if(ptr[i] == 'm')
                escape_seq = false;
        } else if(ptr[i] == '\033') {
            escape_seq = true;
        } else {
            len++;
        }
    }

    return len;
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
