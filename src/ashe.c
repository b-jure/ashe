#include "ashe_string.h"
#include "ashe_utils.h"
#include "jobctl.h"
#include "parser.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define INSIZE 200
#define INT_DIGITS 10

/// Global shell terminal modes
struct termios shell_tmodes;
/// Flag in case we recieve the SIGINT signal
/// while waiting on terminal input
volatile atomic_bool sigint_recv = false;
volatile atomic_bool sigchld_recv = false;

void sigint_handler(__attribute__((unused)) int signum)
{
    sigint_recv = true;
    ATOMIC_PRINT({ pprompt(); });
}

static void init_shell(void)
{
    int shell_pgid;
    int terminal_fd = STDIN_FILENO;
    int shell_is_interactive = isatty(terminal_fd);

    if(shell_is_interactive) {
        if(__glibc_unlikely(!joblist_init())) {
            ATOMIC_PRINT({ pwarn("failed creating a joblist"); });
            exit(EXIT_FAILURE);
        }

        if(__glibc_unlikely(setenv("?", "0", 1) < 0)) {
            ATOMIC_PRINT({
                pwarn("failed creating status environment variable '?'");
                perr();
            });
            exit(EXIT_FAILURE);
        }

        if(__glibc_unlikely(atexit(joblist_drop) != SUCCESS)) {
            ATOMIC_PRINT({
                pwarn("failed initializing shell 'atexit()' functions");
                perr();
            });
            exit(EXIT_FAILURE);
        }

        while(tcgetpgrp(terminal_fd) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        shell_pgid = getpid();
        if(__glibc_unlikely(setpgid(shell_pgid, shell_pgid) < 0)) {
            ATOMIC_PRINT({
                pwarn("failed creating a shell process group");
                perr();
            });
            exit(EXIT_FAILURE);
        }

        if(__glibc_unlikely(
               tcsetpgrp(terminal_fd, shell_pgid) < 0
               || tcgetattr(terminal_fd, &shell_tmodes) < 0))
        {
            ATOMIC_PRINT({ perr(); });
            exit(EXIT_FAILURE);
        }

        struct sigaction new_action;

        new_action.sa_handler = sigint_handler;
        new_action.sa_flags = 0;

        if(__glibc_unlikely(
               sigemptyset(&new_action.sa_mask) < 0
               || sigaction(SIGINT, &new_action, NULL) < 0))
        {
            ATOMIC_PRINT({
                pwarn("failed setting up shell SIGINT signal handler");
                perr();
            });
            exit(EXIT_FAILURE);
        }

        new_action.sa_handler = SIG_IGN;

        if(__glibc_unlikely(
               sigaction(SIGTTIN, &new_action, NULL) < 0
               || sigaction(SIGTTOU, &new_action, NULL) < 0
               || sigaction(SIGQUIT, &new_action, NULL) < 0
               || sigaction(SIGCHLD, &new_action, NULL) < 0))
        {
            ATOMIC_PRINT({
                pwarn("failed setting up shell signal handlers");
                perr();
            });
            exit(EXIT_FAILURE);
        }

        fprintf(stderr, "rest of the handlers assigned\n");
    }
}

int rcmdline(string_t *buffer)
{
    byte input[INSIZE];
    byte *ptr;
    size_t appended;
    bool dq = false;

read_more:
    while(fgets(input, INSIZE, stdin) != NULL) {
        if(__glibc_unlikely(string_len(buffer) + strlen(input) >= ARG_MAX)) {
            ATOMIC_PRINT({ pwarn("commandline argument size %d exceeded", ARG_MAX); });
            return FAILURE;
        } else if(__glibc_unlikely(
                      !string_append(buffer, input, (appended = strlen(input)))))
        {
            return FAILURE;
        } else {
            ptr = string_slice(buffer, string_len(buffer) - appended);
            while(is_some((ptr = strchr(ptr, '"')))) {
                if(char_before_ptr(ptr) != '\\')
                    dq ^= true;
                ptr++;
            }
            if(!dq && string_last(buffer) == '\n')
                break;
        }
    }

    if(sigint_recv || sigchld_recv) {
        string_clear(buffer);
        if(sigchld_recv)
            sigchld_recv = false;
        else
            sigint_recv = false;
        goto read_more;
    }

    return SUCCESS;
}

int main()
{
    commandline_t cmdline;
    string_t *line;
    int status = SUCCESS;
    bool set_env = false;

    struct sigaction sigchld_ac;

    sigemptyset(&sigchld_ac.sa_mask);
    sigchld_ac.sa_flags = 0;
    sigchld_ac.sa_handler = joblist_update_and_notify;

    init_shell();

    if(__glibc_unlikely(is_null(line = string_with_cap(INSIZE)))) {
        ATOMIC_PRINT({ pwarn("couldn't allocate input buffer"); });
        exit(EXIT_FAILURE);
    } else if(__glibc_unlikely(is_null((cmdline = commandline_new()).conditionals))) {
        ATOMIC_PRINT({ pwarn("couldn't allocate internal input storage"); });
        string_drop(line);
        exit(EXIT_FAILURE);
    }

    while(true) {
        // joblist_update_and_notify(0);
        string_clear(line);
        ATOMIC_PRINT({ pprompt(); });

        // Enable async joblist updating
        sigchld_ac.sa_handler = joblist_update_and_notify;
        sigaction(SIGCHLD, &sigchld_ac, NULL); // Can't fail

        if(__glibc_unlikely(rcmdline(line) == FAILURE)) {
            string_drop(line);
            commandline_drop(&cmdline);
            exit(EXIT_FAILURE);
        }

        // Disable async joblist updating
        sigchld_ac.sa_handler = SIG_IGN;
        sigaction(SIGCHLD, &sigchld_ac, NULL); // Can't fail

        if(string_len(line) == 1
           || parse_commandline(string_ref(line), &cmdline, &set_env) == FAILURE)
        {
            commandline_clear(&cmdline);
            continue;
        }

        if(!set_env) {
            commandline_execute(&cmdline, &status);
            byte retstat[INT_DIGITS + 2];
            sprintf(retstat, "%d", status);
            setenv("?", retstat, 1);
        }

        ATOMIC_PRINT({ fprintf(stderr, "return status: %d\n", status); });
        commandline_clear(&cmdline);
    }
}
