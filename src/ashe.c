#include "ashe_string.h"
#include "ashe_utils.h"
#include "jobctl.h"
#include "parser.h"

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/// Global shell terminal modes
struct termios shell_tmodes;

#define PROMPT "\n[-ASHE-]: "
#define INSIZE 200
#define INT_DIGITS 10

static void init_shell(void)
{
    int shell_pgid;
    int terminal_fd = STDIN_FILENO;
    int shell_is_interactive = isatty(terminal_fd);

    if(__glibc_unlikely(!joblist_init())) {
        pwarn("failed creating a joblist");
        exit(EXIT_FAILURE);
    }

    if(__glibc_unlikely(setenv("?", "0", 1) < 0)) {
        pwarn("failed creating status environment variable '?'");
        perr();
        exit(EXIT_FAILURE);
    }

    if(shell_is_interactive) {
        while(tcgetpgrp(terminal_fd) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGINT, SIG_IGN);
        signal(SIGSTOP, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGCHLD, SIG_IGN);

        shell_pgid = getpid();
        if(__glibc_unlikely(setpgid(shell_pgid, shell_pgid) < 0)) {
            pwarn("Couldn't put the shell in its own process group");
            perr();
            exit(EXIT_FAILURE);
        }

        if(__glibc_unlikely(
               tcsetpgrp(terminal_fd, shell_pgid) < 0
               || tcgetattr(terminal_fd, &shell_tmodes) < 0))
        {
            perr();
            exit(EXIT_FAILURE);
        }
    }
}

int rcmdline(string_t *buffer)
{
    byte input[INSIZE];
    byte *ptr;
    size_t appended;
    bool dq = false;

    while(fgets(input, INSIZE, stdin) != NULL) {
        if(__glibc_unlikely(string_len(buffer) + strlen(input) >= ARG_MAX)) {
            pwarn("commandline argument size %d exceeded", ARG_MAX);
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

    if(__glibc_unlikely(ferror(stdin) != 0))
        perr();

    return SUCCESS;
}

int main()
{
    commandline_t cmdline;
    string_t *line;
    int status = SUCCESS;
    bool set_env = false;

    init_shell();

    if(__glibc_unlikely(is_null(line = string_with_cap(INSIZE)))) {
        pwarn("couldn't allocate input buffer");
        exit(EXIT_FAILURE);
    } else if(__glibc_unlikely(is_null((cmdline = commandline_new()).conditionals))) {
        printf("couldn't allocate internal input storage");
        string_drop(line);
        exit(EXIT_FAILURE);
    }

    while(true) {
        printf("%s", PROMPT);
        string_clear(line);

        signal(SIGCHLD, joblist_update_and_notify); /// Enable async joblist updates

        if(__glibc_unlikely(rcmdline(line) == FAILURE)) {
            string_drop(line);
            commandline_drop(&cmdline);
            exit(EXIT_FAILURE);
        }

        if(string_len(line) == 1
           || parse_commandline(string_ref(line), &cmdline, &set_env) == FAILURE)
        {
            commandline_clear(&cmdline);
            continue;
        }

        if(!set_env) {
            signal(SIGCHLD, SIG_DFL); // Disable async joblist updating
            commandline_execute(&cmdline, &status);
            /// Max amount of digits in int on x86_64 bit machine
            /// including sign + null terminator space
            byte retstat[INT_DIGITS + 2];
            sprintf(retstat, "%d", status);
            /// Can't fail we already created the var in shell_init
            setenv("?", retstat, 1);
        }

        printf("Finished executing commandline with status: %d\n", status);
        commandline_clear(&cmdline);
    }
}
