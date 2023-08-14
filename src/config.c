#include "config.h"
#include "lexer.h"
#include "shell.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define HOME_CFG_PATH ".config/ashe/config.ashe"
#define ETC_CFG_PATH  "/etc/ashe/config.ashe"

string_t* config_getvar(int fd, int varidx)
{
    byte buff[BUFSIZ];
    memset(buff, 0, BUFSIZ);

    byte*     ptr = NULL;
    string_t* msg = string_new();
    if(__glibc_unlikely(is_null(msg))) {
        exit(EXIT_FAILURE);
    }

    const byte* var = config_var(varidx);

    if(var == NULL) {
        return NULL;
    }

    int64_t i, n;
    int16_t c;
    bool    dq, esc, first;

    dq    = false;
    esc   = false;
    first = true;

    while((n = read(fd, buff, BUFSIZ - 1)) > 0) {
        buff[n] = '\0';
        ptr     = strstr(buff, var);

        if(is_some(ptr)) {
            size_t varlen = strlen(var);
            ptr += varlen;

            while(isspace(*ptr)) {
                if(*ptr++ == '\n') return NULL;
            }

        read:
            buff[n] = '\0';
            for(i = ptr - buff; i < BUFSIZ; i++) {
                c = buff[i];
                if((!dq && !esc && c == '\n') || c == EOF) break;
                else if(!esc && c == '"') dq ^= true;
                else if(c == '\\' || esc) esc ^= true;
            }

            if(i == BUFSIZ) {
                if(__glibc_unlikely(!string_append(msg, ptr, i - ((first) ? varlen : 0)))) {
                    exit(EXIT_FAILURE);
                } else if(__glibc_unlikely((n = read(fd, buff, BUFSIZ - 1)) < 0)) {
                    die();
                } else if(n == 0) {
                    if(!dq) break;
                    goto err;
                } else {
                    ptr   = buff;
                    first = false;
                    goto read;
                }
            } else {
                if(dq) goto err;
                if(__glibc_unlikely(!string_append(msg, ptr, i - (ptr - buff)))) exit(EXIT_FAILURE);
                break;
            }
        }
    }

    if(__glibc_unlikely(n < 0)) die();
    else if(string_len(msg) == 0) return NULL;
    else return msg;

err:
    pwarn("failed processing config variable '%s'", var);
    string_drop(msg);
    exit(EXIT_FAILURE);
}

int config_open(void)
{
    byte* home_dir = getenv(HOME);
    if(__glibc_unlikely(is_null(home_dir))) {
        die();
    }

    size_t len = strlen(home_dir);
    byte   path[len + sizeof(HOME_CFG_PATH) + 1];
    sprintf(path, "%s%s", home_dir, HOME_CFG_PATH);

    int fd = open(path, O_RDONLY);

    if(fd < 0) {
        fd = open(ETC_CFG_PATH, O_CREAT | O_RDONLY, S_IRUSR);
        if(__glibc_unlikely(fd < 0)) {
            return -1;
        }
    }

    return fd;
}

const byte* config_var(int vardix)
{
    static const byte* cfg_vars[] = {
        "ASHE_WELCOME=",
        "ASHE_PROMPT=",
    };

    return cfg_vars[vardix];
}
