#include "ashe_string.h"
#include "async.h"
#include "errors.h"
#include "shell.h"

#include <ctype.h>
#include <string.h>

#define CV_WELCOME 0

static void        shell_cleanup(void);
static void        pwelcome(void);
static const byte* config_var(int varidx);
static string_t*   config_getvar(int fd, int varidx);

shell_t shell = {0};

void shell_init(void)
{
    pid_t sh_pgid              = getpgrp();
    int   shell_is_interactive = isatty(STDIN_FILENO);

    if(shell_is_interactive) {
        /* Initialize the parser storage */
        shell.sh_cmdline = commandline_new();
        if(__glibc_unlikely(is_null(shell.sh_cmdline.conditionals))) exit(EXIT_FAILURE);

        /* Unset the interrupt flag */
        shell.sh_intr = false;

        /* Initialize the joblist */
        shell.sh_jlist = joblist_init();
        if(__glibc_unlikely(is_null(shell.sh_jlist.jobs))) exit(EXIT_FAILURE);

        /* Initialize terminal modes/storage/state */
        terminal_init(&shell.sh_term);

        /* Initialize/Insert return status variable */
        if(__glibc_unlikely(setenv("?", "0", 1) < 0)) {
            ATOMIC_PRINT({
                PW_STATVAR_INIT;
                die();
            });
        }

        /* Setup shell cleanup */
        if(__glibc_unlikely(atexit(shell_cleanup))) {
            ATOMIC_PRINT({
                PW_SHCLEANUP_INIT;
                die();
            });
        }

        /* Loop and stop this process group until shell
         * process group is in control of the terminal */
        while(tcgetpgrp(STDIN_FILENO) != (sh_pgid = getpgrp())) kill(-sh_pgid, SIGTTIN);

        /* Move shell process into its own process group */
        if(__glibc_unlikely(setpgid(getpid(), sh_pgid) < 0)) {
            ATOMIC_PRINT({
                PW_PGRPSET(getpid(), sh_pgid);
                die();
            });
        }

        /* Set the shell process group ID as the foreground process group ID of the terminal */
        if(__glibc_unlikely(tcsetpgrp(STDIN_FILENO, sh_pgid) < 0)) ATOMIC_PRINT(die());

        /* Setup shell signal handling (async) */
        setup_default_signal_handling();

        pwelcome();
    }
}

static void pwelcome(void)
{
    byte* config_path = getenv("HOME");

    if(__glibc_unlikely(is_null(config_path))) {
        die();
    }

    size_t len = strlen(config_path);
    byte   path[len + sizeof(".config/ashe/config.ashe") + 1];
    sprintf(path, "%s/.config/ashe/config.ashe", config_path);

    int fd = open(path, O_RDONLY);

    if(fd < 0) {
        fd = open("/etc/ashe/config.ashe", O_CREAT | O_RDONLY, S_IRUSR);
        if(__glibc_unlikely(fd < 0)) {
            return;
        }
    }

    string_t* msg = config_getvar(fd, CV_WELCOME);

    if(is_null(msg)) {
        pwarn("config file error, failed processing welcome message");
        exit(EXIT_FAILURE);
    }

    unescape(string_slice(msg, 0));
    fprintf(stderr, "%s\n", string_ref(msg));
    string_drop(msg);
}

static const byte* config_var(int vardix)
{
    static const byte* cfg_vars[] = {
        "ASHE_WELCOME=",
    };

    return cfg_vars[vardix];
}

static string_t* config_getvar(int fd, int varidx)
{
    byte        buff[BUFSIZ];
    byte*       ptr = NULL;
    string_t*   msg = string_with_cap(10);
    const byte* var = config_var(varidx);

    if(__glibc_unlikely(is_null(msg))) exit(EXIT_FAILURE);

    int64_t i, n;
    int16_t c;
    bool    dq, esc, first;

    dq    = false;
    esc   = false;
    first = true;

    while((n = read(fd, buff, BUFSIZ)) > 0) {
        ptr = strstr(buff, var);

        if(is_some(ptr)) {
            size_t varlen = strlen(var);
            ptr += varlen;

            while(isspace(*ptr)) {
                if(*ptr++ == '\n') return msg;
            }

        read:
            for(i = ((first) ? varlen : 0); i < BUFSIZ; i++) {
                c = buff[i];
                if(!dq && !esc && c == '\n') {
                    break;
                } else if(!esc && c == '"') dq ^= true;
                else if(c == '\\' || esc) esc ^= true;
            }

            if(i == BUFSIZ) {
                if(__glibc_unlikely(!string_append(msg, ptr, i - ((first) ? varlen : 0)))) {
                    exit(EXIT_FAILURE);
                } else if(__glibc_unlikely((n = read(fd, buff, BUFSIZ)) < 0)) {
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
                if(__glibc_unlikely(!string_append(msg, ptr, i))) exit(EXIT_FAILURE);
                break;
            }
        }
    }

    return msg;

err:
    string_drop(msg);
    return NULL;
}

static void shell_cleanup(void)
{
    /* Free the joblist storage and reap any running processes */
    joblist_drop(&shell.sh_jlist);
    /* Free the parser storage */
    commandline_drop(&shell.sh_cmdline);
}
