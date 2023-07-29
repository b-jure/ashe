#include "ashe_string.h"
#include "async.h"
#include "errors.h"
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
    set_dflmode();
    joblist_drop();
}

/// TODO: Put everyhing in a new terminal_t struct (Data oriented design)
static void init_shell(void)
{
    pid_t shell_pgid = getpgrp();
    int terminal_fd = STDIN_FILENO;
    int shell_is_interactive = isatty(terminal_fd);

    if(shell_is_interactive) {
        if(__glibc_unlikely(!joblist_init())) {
            ATOMIC_PRINT(PW_JLINIT);
            exit(EXIT_FAILURE);
        }

        if(__glibc_unlikely(setenv("?", "0", 1) < 0)) {
            ATOMIC_PRINT({
                PW_STATVAR_INIT;
                die();
            });
        }

        if(__glibc_unlikely(atexit(cleanup))) {
            ATOMIC_PRINT({
                PW_SHCLEANUP_INIT;
                die();
            });
        }

        while(tcgetpgrp(terminal_fd) != shell_pgid)
            kill(-shell_pgid, SIGTTIN);

        if(__glibc_unlikely(setpgid(getpid(), shell_pgid) < 0)) {
            ATOMIC_PRINT({
                PW_PGRPSET(getpid(), shell_pgid);
                die();
            });
        }

        if(__glibc_unlikely(tcsetpgrp(terminal_fd, shell_pgid) < 0))
            ATOMIC_PRINT(die());

        init_dflterm();
        init_rawterm();
        setup_default_signal_handling();
    }
}

int main()
{
    int status = SUCCESS;

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
        read_input(&terminal_input);

        try_wait_missed_sigchld_signals();

        string_clear(line);
        string_append(line, terminal_input.buffer, terminal_input.len);
        inbuff_clear(&terminal_input);

        disable_async_joblist_update();

        if(string_len(line) == 1 || parse_commandline(string_ref(line), &cmdline) == FAILURE) {
            commandline_clear(&cmdline);
            continue;
        }

        commandline_execute(&cmdline, &status);
        byte retstat[INT_DIGITS + 2];
        sprintf(retstat, "%d", status);
        setenv("?", retstat, 1);

        commandline_clear(&cmdline);
    }
}
