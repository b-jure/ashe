#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"
#include "shell.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

void sigwin_handler(int signum)
{
    unused(signum);
    mask_signal(SIGCHLD, SIG_BLOCK);
    mask_signal(SIGINT, SIG_BLOCK);
    ashe.sh_int = 1;
    get_size_or_die(&ashe.sh_term.tm_rows, &ashe.sh_term.tm_columns);
    mask_signal(SIGCHLD, SIG_UNBLOCK);
    mask_signal(SIGINT, SIG_UNBLOCK);
}

void sigint_handler(int signum)
{
    unused(signum);
    mask_signal(SIGWINCH, SIG_BLOCK);
    mask_signal(SIGCHLD, SIG_BLOCK);
    ashe.sh_int = 1;
    TerminalInput_gotoend(&ashe.sh_term.tm_input);
    fprintf(stderr, "\r\n");
    print_prompt();
    TerminalInput_clear(&ashe.sh_term.tm_input);
    mask_signal(SIGWINCH, SIG_UNBLOCK);
    mask_signal(SIGCHLD, SIG_UNBLOCK);
}

void sigchld_handler(int signum)
{
    mask_signal(SIGWINCH, SIG_BLOCK);
    mask_signal(SIGINT, SIG_BLOCK);
    ashe.sh_int = true;
    if(unlikely(get_cursor_pos(NULL, &ashe.sh_term.tm_col))) {
        exit(EXIT_FAILURE);
    } else {
        Joblist_update_and_notify(&ashe.sh_jlist, signum);
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
    mask_signal(SIGCHLD, SIG_BLOCK);
    mask_signal(SIGINT, SIG_BLOCK);
    mask_signal(SIGWINCH, SIG_BLOCK);
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

    if(unlikely(sigaction(SIGINT, &default_action, NULL) < 0)) {
        PW_SIGINITS("SIGINT");
        die();
    }

    default_action.sa_handler = SIG_DFL;

    if(unlikely(sigaction(SIGCHLD, &default_action, NULL) < 0)) {
        PW_SIGINITS("SIGCHLD");
        die();
    }

    default_action.sa_handler = SIG_IGN;

    if(unlikely(
           sigaction(SIGTTIN, &default_action, NULL) < 0 || sigaction(SIGTTOU, &default_action, NULL) < 0
           || sigaction(SIGTSTP, &default_action, NULL) < 0 || sigaction(SIGQUIT, &default_action, NULL) < 0))
    {
        PW_SIGINIT;
        die();
    }
}

void try_wait_missed_sigchld_signals(void)
{
    Joblist_update_and_notify(&ashe.sh_jlist, 0);
}
