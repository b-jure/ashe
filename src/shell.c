#include "async.h"
#include "errors.h"
#include "shell.h"

static void shell_cleanup(void);

shell_t shell = {0};

void shell_init(void)
{
    pid_t sh_pgid              = getpgrp();
    int   shell_is_interactive = isatty(STDIN_FILENO);

    if(shell_is_interactive) {
        /* Initialize the parser storage */
        shell.sh_cmdline = commandline_new();
        if(__glibc_unlikely(is_null(shell.sh_cmdline.conditionals)))
            exit(EXIT_FAILURE);

        /* Unset the interrupt flag */
        shell.sh_intr = false;

        /* Initialize the joblist */
        shell.sh_jlist = joblist_init();
        if(__glibc_unlikely(is_null(shell.sh_jlist.jobs)))
            exit(EXIT_FAILURE);

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
        if(__glibc_unlikely(tcsetpgrp(STDIN_FILENO, sh_pgid) < 0))
            ATOMIC_PRINT(die());

        /* Setup shell signal handling (async) */
        setup_default_signal_handling();
    }
}

static void shell_cleanup(void)
{
    /* Free the joblist storage and reap any running processes */
    joblist_drop(&shell.sh_jlist);
    /* Free the parser storage */
    commandline_drop(&shell.sh_cmdline);
}
