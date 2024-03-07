#include "aconf.h"
#include "async.h"
#include "errors.h"
#include "shell.h"
#include "aalloc.h"


#if !defined(ASHE_STATUS_VAR)
#define ASHE_STATUS_VAR "?"
#endif

#if !defined(ASHE_WELCOME)
#define _ASHE_WELCOME "ashe: Hi %user you are truly a gigachad!"
#else
#define _ASHE_WELCOME ASHE_WELCOME ""
#endif


/* Shell global */
Shell ashe = {0};


static void print_welcome(void)
{
    static Buffer buffer;

    if(buffer.cap == 0 && sizeof(ASHE_WELCOME) > 1) {
        Buffer_init(&buffer);
        Buffer_init_cap(&buffer, sizeof(ASHE_WELCOME));
        Buffer_push_str(&buffer, ASHE_WELCOME, sizeof(ASHE_WELCOME) - 1);
        unescape(&buffer);
        ArrayCharptr_insert(&ashe.sh_buffers, 0, buffer.data);
    }

    // TODO: process placeholders
    // TODO: print the buffer
    ashe.sh_buffers.data[0] = buffer.data; // update
}


static void wrapped_aalloc(void* ptr)
{
    aalloc(ptr, 0);
}

void Shell_cleanup(void)
{
    Joblist_drop(&ashe.sh_jlist);
    ArrayCharptr_free(&ashe.sh_buffers, wrapped_aalloc);
    Lexer_free(&ashe.sh_lexer);
}

void Fork_cleanup(void)
{
    // Implement
}


void Shell_init(Shell* sh)
{
    pid_t sh_pgid = getpgrp();
    int shell_is_interactive = isatty(STDIN_FILENO);

    if(shell_is_interactive) {
        Joblist_init(&sh->sh_jlist);
        Terminal_init(&sh->sh_term);

        /* Setup status environment variable */
        if(unlikely(setenv(ASHE_STATUS_VAR, "0", 1) < 0)) {
            fprint_warning("failed creating status environment variable" bold(bred(ASHE_STATUS_VAR)));
            die();
        }

        /* Setup shell cleanup, just to be polite and
         * provide forked processes with proper cleanup. */
        if(unlikely(atexit(shell_cleanup))) {
            PW_SHCLEANUP_INIT;
            die();
        }

        /* Loop and stop this process group until shell
         * process group is in control of the terminal */
        while(tcgetpgrp(STDIN_FILENO) != (sh_pgid = getpgrp()))
            kill(-sh_pgid, SIGTTIN);

        /* Move shell process into its own process group */
        if(unlikely(setpgid(getpid(), sh_pgid) < 0)) {
            ATOMIC_PRINT({
                PW_PGRPSET(getpid(), sh_pgid);
                die();
            });
        }

        /* Set the shell process group ID as the foreground process group ID of the terminal */
        if(unlikely(tcsetpgrp(STDIN_FILENO, sh_pgid) < 0)) ATOMIC_PRINT(die());

        /* Setup shell signal handling (async) */
        setup_default_signal_handling();

        /* Print welcome message if it is set */
        print_welcome();
    }
}

