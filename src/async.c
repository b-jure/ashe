#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"
#include "shell.h"

#include <signal.h>
#include <stdbool.h>

void sigwin_handler(__attribute__((unused)) int signum)
{
    mask_signal(SIGCHLD, SIG_BLOCK);
    mask_signal(SIGINT, SIG_BLOCK);
    shell.sh_intr = true;
    get_size_or_die(&terminal.tm_rows, &terminal.tm_columns);
    mask_signal(SIGCHLD, SIG_UNBLOCK);
    mask_signal(SIGINT, SIG_UNBLOCK);
}

void sigint_handler(__attribute__((unused)) int signum)
{
    mask_signal(SIGWINCH, SIG_BLOCK);
    mask_signal(SIGCHLD, SIG_BLOCK);
    shell.sh_intr = true;
    ATOMIC_PRINT({
        inbuff_goto_end(&inbuff);
        fprintf(stderr, "\r\n");
        pprompt();
    });
    inbuff_clear(&terminal.tm_inbuff);
    mask_signal(SIGWINCH, SIG_UNBLOCK);
    mask_signal(SIGCHLD, SIG_UNBLOCK);
}

void sigchld_handler(int signum)
{
    mask_signal(SIGWINCH, SIG_BLOCK);
    mask_signal(SIGINT, SIG_BLOCK);
    shell.sh_intr = true;
    if(__glibc_unlikely(get_cursor_pos(NULL, &terminal.tm_col))) {
        exit(EXIT_FAILURE);
    } else {
        joblist_update_and_notify(signum);
        mask_signal(SIGWINCH, SIG_UNBLOCK);
        mask_signal(SIGINT, SIG_UNBLOCK);
    }
}

void unblock_signals(void)
{
    mask_signal(SIGCHLD, SIG_UNBLOCK);
    mask_signal(SIGINT, SIG_UNBLOCK);
    mask_signal(SIGWINCH, SIG_UNBLOCK);
}

void block_signals(void)
{
    mask_signal(SIGCHLD, SIG_UNBLOCK);
    mask_signal(SIGINT, SIG_UNBLOCK);
    mask_signal(SIGWINCH, SIG_UNBLOCK);
}

void mask_signal(int signum, int how)
{
    sigset_t signal;
    sigemptyset(&signal);
    sigaddset(&signal, signum);
    sigprocmask(how, &signal, NULL);
}

void enable_async_joblist_update(void)
{
    struct sigaction old_action;
    sigaction(SIGCHLD, NULL, &old_action);
    old_action.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &old_action, NULL);
}

void disable_async_joblist_update(void)
{
    struct sigaction old_action;
    sigaction(SIGCHLD, NULL, &old_action);
    old_action.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &old_action, NULL);
}

void setup_default_signal_handling(void)
{
    struct sigaction default_action;
    sigemptyset(&default_action.sa_mask);
    default_action.sa_handler = sigint_handler;
    default_action.sa_flags   = 0;

    if(__glibc_unlikely(sigaction(SIGINT, &default_action, NULL) < 0)) {
        ATOMIC_PRINT({
            PW_SIGINITS("SIGINT");
            die();
        });
    }

    default_action.sa_handler = SIG_DFL;

    if(__glibc_unlikely(sigaction(SIGCHLD, &default_action, NULL) < 0)) {
        ATOMIC_PRINT({
            PW_SIGINITS("SIGCHLD");
            die();
        });
    }

    default_action.sa_handler = SIG_IGN;

    if(__glibc_unlikely(
           sigaction(SIGTTIN, &default_action, NULL) < 0 || sigaction(SIGTTOU, &default_action, NULL) < 0
           || sigaction(SIGTSTP, &default_action, NULL) < 0 || sigaction(SIGQUIT, &default_action, NULL) < 0))
    {
        ATOMIC_PRINT({
            PW_SIGINIT;
            die();
        });
    }
}

void try_wait_missed_sigchld_signals(void)
{
    joblist_update_and_notify(0);
}
