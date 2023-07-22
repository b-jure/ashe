#define _XOPEN_SOURCE
#include "ashe_string.h"
#include "ashe_utils.h"
#include "parser.h"
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/// Global joblist
extern joblist_t joblist;
/// Global shell terminal modes
struct termios shell_tmodes;

#define PROMPT "\n[-ASHE-]: "
#define INSIZE 200

static void init_shell(void)
{
    int shell_pgid;
    int terminal_fd = STDIN_FILENO;
    int shell_is_interactive = isatty(terminal_fd);

    if(is_null(joblist.jobs = vec_new(sizeof(job_t)))) {
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
        if(setpgid(shell_pgid, shell_pgid) < 0) {
            pwarn("Couldn't put the shell in its own process group");
            perr();
            exit(EXIT_FAILURE);
        }

        tcsetpgrp(terminal_fd, shell_pgid);
        tcgetattr(terminal_fd, &shell_tmodes);
    }
}

int rcmdline(string_t *buffer)
{
    byte input[INSIZE];
    byte *ptr;
    size_t appended;
    bool dq = false;

    while(fgets(input, INSIZE, stdin) != NULL) {
        if(string_len(buffer) + strlen(input) >= ARG_MAX) {
            pwarn("commandline argument size %d exceeded", ARG_MAX);
            return FAILURE;
        } else if(!string_append(buffer, input, (appended = strlen(input)))) {
            return FAILURE;
        } else {
            ptr = string_slice(buffer, string_len(buffer) - appended);
            while(is_some((ptr = strchr(ptr, '"')))) {
                if(char_before_ptr(ptr) != '\\') {
                    dq ^= true;
                }
                ptr++;
            }
            if(!dq && string_last(buffer) == '\n') {
                break;
            }
        }
    }

    if(ferror(stdin) != 0) {
        /// Don't exit with failure just warn
        pwarn("error occured while reading the command line");
    }

    return SUCCESS;
}

int main()
{
    commandline_t cmdline;
    string_t *line;
    int status = SUCCESS;
    bool set_env = false;

    init_shell();

    if(is_null(line = string_with_cap(INSIZE))) {
        exit(EXIT_FAILURE);
    }

    if(is_null((cmdline = commandline_new()).conditionals)) {
        string_drop(line);
        exit(EXIT_FAILURE);
    }

    while(true) {
        printf("%s", PROMPT);
        string_clear(line);

        if(rcmdline(line) == FAILURE) {
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
            commandline_execute(&cmdline, &status);
        }
        printf("Finished executing commandline with status: %d\n", status);
        commandline_clear(&cmdline);
    }
}
