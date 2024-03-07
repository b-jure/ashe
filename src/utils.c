#include "acommon.h"
#include "ashe_utils.h"
#include "input.h"
#include "async.h"
#include "shell.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>



static void print(FILE* stream, const char* fmt, ...)
{
}

static void vprint(FILE* stream, const char* fmt, va_list argp)
{
}

void print_error_wprefix(const char* errfmt, va_list argp)
{
    fprintf(stderr, ASHE_ERR_PREFIX " ");
    vfprintf(stderr, errfmt, argp);
    fputc('\n', stderr);
    fflush(stderr);
}

void print_error(const char* errfmt, ...)
{
    block_signals();
    va_list argp;
    va_start(argp, errfmt);
    print_error_wprefix(errfmt, argp);
    va_end(argp);
    unblock_signals();
    try_wait_missed_sigchld_signals();
}

void print_errno(void)
{
    block_signals();
    ashe_assert(errno != 0, "invalid errno");
    const char* errmsg = strerror(errno);
    print_error(stderr, errmsg); // unblock_signals() in here
}


void die_err(const char* err)
{
    block_signals();
    panic(err);
}

void die(void)
{
    block_signals();
    panic(NULL);
}


void print_warning_wprefix(const char* wfmt, va_list argp)
{
    fprintf(stderr, ASHE_WARN_PREFIX " ");
    vfprintf(stderr, wfmt, argp);
    fputc('\n', stderr);
    fflush(stderr);
}

void vfprint_warning(const char* wfmt, va_list argp)
{
    block_signals();
    print_warning_wprefix(wfmt, argp);
    unblock_signals();
    try_wait_missed_sigchld_signals();
}

void fprint_warning(const char* wfmt, ...)
{
    block_signals();
    va_list argp;
    va_start(argp, wfmt);
    print_warning_wprefix(wfmt, argp);
    va_end(argp);
    unblock_signals();
    try_wait_missed_sigchld_signals();
}


void print_info(Info type, const char* str)
{
    block_signals();
    static const char* infostr[] = {
        "NAME",
        "DESCRIPTION",
        "SYNOPSIS",
    };

    const char* info = infostr[type];
    memmax len = strlen(str);
    char buffer[len + 1];
    memcpy(buffer, str, len + 1);

    uint32 col_limit = ashe.sh_term.tm_columns - 10;
    memmax cap = len + (len / col_limit) + 2;
    char buff[cap];
    char* target = buff;
    buff[0] = '\0';
    memset(buff, 0, cap);

    uint32 remaining_space = col_limit;
    const char* delimiter = " ";
    const char* word = strtok(buffer, delimiter);
    char* ptr = NULL;

    while(word) {
        memmax word_len = len_without_seq(word);
        if(remaining_space > word_len) {
            target += sprintf(target, "%s ", word);
            remaining_space -= (word_len + 1); // Account for delimiter (1)
        } else {
            target += sprintf(target, "\n%s ", word);
            remaining_space = col_limit - word_len - 1; // Account for delimiter (1)
        }
        if((ptr = strchr(word, '\n')) != NULL) {
            remaining_space = col_limit - strlen(ptr + 1);
        }
        word = strtok(NULL, delimiter);
    }

    fprintf(stderr, "\n" yellow(bold("%s")) "\n%-s\n", info, buff);
    fflush(stderr);
    unblock_signals();
    try_wait_missed_sigchld_signals();
}


void print_manpage(const char* name, const char* usage, const char* desc)
{
    print_info(INFO_NAME, name);
    print_info(INFO_USAGE, usage);
    print_info(INFO_DESC, desc);
}


memmax len_without_seq(const char* ptr)
{
    memmax len = 0;
    ubyte escape_seq = 0;
    for(memmax i = 0; ptr[i]; i++) {
        if(escape_seq) {
            if(ptr[i] == 'm') escape_seq = 0;
        } else if(ptr[i] == '\033') {
            escape_seq = 1;
        } else {
            len++;
        }
    }
    return len;
}


void expand_vars(Buffer* buffer)
{
    memmax len = buffer->len;
    char* ptr = buffer->data;
    const char* value = NULL;

    for(; ((ptr = strchr(ptr, '$')) != NULL); ptr++) {
        if(is_escaped(ptr, ptr - buffer->data)) continue;
        memmax offset = strspn(++ptr, ENV_VAR_CHARS);

        if(offset == 0) continue;
        char* end = ptr + offset;
        char cached = *end;
        *end = '\0';
        value = getenv(ptr);
        *end = cached;

        --ptr; // go back to '$'
        int32 klen = offset + 1; // key chars + '$'

        if(value != NULL) { // found value ?
            int32 vlen = strlen(value);
            ssize diff = vlen - klen;

            if(diff > 0) {
                if(likely(buffer->len + diff < ARG_MAX)) {
                    Buffer_ensure(buffer, diff);
                    memmove(end + diff, end, diff);
                    buffer->len += diff;
                } else goto l_remove;
            } else {
                diff = -diff;
                memcpy(end - diff, end, diff);
                buffer->len -= diff;
            }

            memcpy(ptr, value, vlen);
        } else { // no variable found, remove the whole key together with '$'
        l_remove:
            memcpy(ptr, end, klen);
            buffer->len -= klen;
        }
    }
}


ubyte in_dq(char* str, memmax len)
{
    ubyte dq = 0;
    while(len--)
        if(*str++ == '"') dq ^= 1;
    return dq;
}


ubyte is_escaped(char* bt, memmax curpos)
{
    char* at = bt + curpos;
    return ((curpos > 1 && at[-1] == '\\' && at[-2] != '\\') || (curpos == 1 && at[-1] == '\\'));
}


void unescape(Buffer* buffer)
{
    static const int32 escape[UINT8_MAX] = {
        ['a'] = '\a',
        ['b'] = '\b',
        ['f'] = '\f',
        ['n'] = '\n',
        ['r'] = '\r',
        ['t'] = '\t',
        ['v'] = '\v',
        ['\\'] = '\\',
        ['\''] = '\'',
        ['"'] = '\"',
        ['?'] = '\?',
    };

    char *oldp, *newp;
    oldp = newp = buffer->data;

    while(*oldp) {
        int32 c = *(unsigned char*)oldp++;
        if(c == '"') continue;
        if(c == '\\') {
            c = *(unsigned char*)oldp;
            if(c == '\0') {
                break;
            } else if(c == '0' && oldp[1] == '3' && oldp[2] == '3') {
                c = '\033';
                oldp += 3;
            } else if(escape[c]) {
                c = escape[c];
                oldp++;
            }
        }
        *newp++ = c;
    }
    *newp = '\0';
    buffer->len = (newp - buffer->data) + 1;
}

