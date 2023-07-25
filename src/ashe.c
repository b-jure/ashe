#include "ashe_string.h"
#include "async.h"
#include "input.h"
#include "jobctl.h"
#include "parser.h"

#include <string.h>
#include <termios.h>

#define INT_DIGITS 10

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

        if(__glibc_unlikely(atexit(joblist_drop) != SUCCESS)) {
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

        if(__glibc_unlikely(
               tcsetpgrp(terminal_fd, shell_pgid) < 0
               || tcgetattr(terminal_fd, &shell_tmodes) < 0))
        {
            ATOMIC_PRINT({ perr(); });
            exit(EXIT_FAILURE);
        }

        setup_default_signal_handling();
    }
}

int main()
{
    commandline_t cmdline;
    string_t *line;
    int status = SUCCESS;
    bool set_env = false;

    if(__glibc_unlikely(is_null(line = string_with_cap(INSIZE)))) {
        ATOMIC_PRINT({ pwarn("couldn't allocate input buffer"); });
        exit(EXIT_FAILURE);
    } else if(__glibc_unlikely(is_null((cmdline = commandline_new()).conditionals))) {
        ATOMIC_PRINT({ pwarn("couldn't allocate internal input storage"); });
        string_drop(line);
        exit(EXIT_FAILURE);
    }

    init_shell();

    while(true) {
        ATOMIC_PRINT({ pprompt(); });
        enable_async_joblist_update();
        try_wait_missed_sigchld_signals();

        /// Clear the buffer and start reading
        string_clear(line);
        if(__glibc_unlikely(read_input(line) == FAILURE)) {
            string_drop(line);
            commandline_drop(&cmdline);
            exit(EXIT_FAILURE);
        }

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

        ATOMIC_PRINT({ fprintf(stderr, "return status: %d\n", status); });
        commandline_clear(&cmdline);
    }
}
