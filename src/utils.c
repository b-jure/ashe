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

void expand_vars(byte** word)
{
    size_t         len   = strlen(*word);
    register byte* ptr   = *word;
    register byte* start = NULL;
    register byte* end   = NULL;
    const byte*    value = NULL;

    for(; is_some((ptr = strchr(ptr, '$'))); ptr++) {
        if(is_escaped(ptr, ptr - *word)) {
            continue;
        }

        size_t offset = strspn(ptr + 1, PORTABLE_CHARACTER_SET);

        if(offset == 0 || __glibc_unlikely((end = (start = ptr + 1) + offset) - *word >= ARG_MAX)) {
            continue;
        }

        byte cached     = *end;
        *end            = NULL_TERM;
        int32_t key_len = strlen(start) + 1; // Account for '$'
        *end            = cached;

        value = getenv(start);

        if(is_some(value)) {
            int32_t var_len = strlen(value);
            int32_t diff    = var_len - key_len;

            if(diff > 0) {
                if(__glibc_likely(len + diff < ARG_MAX)) {
                    memcpy(end, end + diff, diff);
                } else {
                    continue;
                }
            } else {
                diff = abs(diff);
                memcpy(end - diff, end, diff);
            }

            memcpy(ptr, value, var_len);
        } else {
            /* If no variable found remove the whole key indicated with '$' */
            memcpy(ptr, end, key_len);
        }
    }
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
            if(ptr[i] == 'm') escape_seq = false;
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
        if(*str++ == '"') dq ^= true;
    return dq;
}

bool is_escaped(byte* bt, size_t curpos)
{
    byte* at = bt + curpos;
    return ((curpos > 1 && *(at - 1) == '\\' && *(at - 2) != '\\') || (curpos == 1 && *(at - 1) == '\\'));
}

int unescape(byte* str)
{
    static const byte escape[256] = {
        ['a']  = '\a',
        ['b']  = '\b',
        ['f']  = '\f',
        ['n']  = '\n',
        ['r']  = '\r',
        ['t']  = '\t',
        ['v']  = '\v',
        ['\\'] = '\\',
        ['\''] = '\'',
        ['"']  = '\"',
        ['?']  = '\?',
    };

    byte* ptr = str;
    byte* q   = str;

    while(*ptr) {
        int c = *(unsigned char*) ptr++;

        if(c == '\\' || c == '"') {

            if(c == '"') {
                continue;
            }

            c = *(unsigned char*) ptr;

            if(c == '\0') {
                break;
            } else if(c == '0' && *(ptr + 1) == '3' && *(ptr + 2) == '3') {
                c = '\033';
                ptr += 3;
            } else if(escape[c]) {
                c = escape[c];
            }
        }

        *q++ = c;
    }

    *q = '\0';
    return ptr - q;
}

void perr(void)
{
    perror(ASHE_ERR_PREFIX);
}
