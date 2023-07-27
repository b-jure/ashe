#include "ashe_string.h"
#include "async.h"
#include "input.h"
#include "jobctl.h"
#include "parser.h"

#include <string.h>
#include <termios.h>

#define INT_DIGITS 10

/// Must be global so that signal
/// handlers have access to it
inbuff_t terminal_input = {0};

static void cleanup(void)
{
    joblist_drop();
}

static void init_shell(void)
{
    pid_t shell_pgid = getpgrp();
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

        if(__glibc_unlikely(atexit(cleanup))) {
            ATOMIC_PRINT({
                pwarn("failed setting up shell cleanup routine");
                perr();
            });
            exit(EXIT_FAILURE);
        }

        while(tcgetpgrp(terminal_fd) != shell_pgid)
            kill(-shell_pgid, SIGTTIN);

        if(__glibc_unlikely(setpgid(getpid(), shell_pgid) < 0)) {
            ATOMIC_PRINT({
                pwarn("failed creating a shell process group");
                perr();
            });
            exit(EXIT_FAILURE);
        }

        if(__glibc_unlikely(tcsetpgrp(terminal_fd, shell_pgid) < 0)) {
            ATOMIC_PRINT(perr());
            exit(EXIT_FAILURE);
        }

        init_dflterm();
        init_rawterm();
        setup_default_signal_handling();
    }
}

int main()
{
    int status = SUCCESS;
    bool set_env = false;

    string_t *line = string_new();
    if(__glibc_unlikely(is_null(line)))
        exit(EXIT_FAILURE);

    commandline_t cmdline = commandline_new();
    if(__glibc_unlikely(is_null(cmdline.conditionals))) {
        string_drop(line);
        exit(EXIT_FAILURE);
    }

    init_shell();

    while(true) {
        ATOMIC_PRINT(pprompt());
        enable_async_joblist_update();

        try_wait_missed_sigchld_signals();

        inbuff_clear(&terminal_input);
        if(__glibc_unlikely(read_input(&terminal_input) == FAILURE)) {
            string_drop(line);
            commandline_drop(&cmdline);
            exit(EXIT_FAILURE);
        }

        try_wait_missed_sigchld_signals();

        string_clear(line);
        string_append(line, terminal_input.buffer, terminal_input.len);
        inbuff_clear(&terminal_input);

        disable_async_joblist_update();

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

        commandline_clear(&cmdline);
    }
}
