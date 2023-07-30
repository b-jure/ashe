#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"

#include <signal.h>
#include <stdbool.h>

volatile atomic_bool sigint_recv = false;
volatile atomic_bool sigchld_recv = false;

void sigint_handler(__attribute__((unused)) int signum)
{
    block_sigchld();
    sigint_recv = true;
    ATOMIC_PRINT({
        fprintf(stderr, "\r\n");
        pprompt();
    });
    inbuff_clear(&terminal_input);
    unblock_sigchld();
}

void sigchld_handler(int signum)
{
    block_sigint();
    sigchld_recv = true;
    joblist_update_and_notify(signum);
    unblock_sigint();
}

void block_sigint(void)
{
    sigset_t block_sigint;
    sigemptyset(&block_sigint);
    sigaddset(&block_sigint, SIGINT);
    sigprocmask(SIG_BLOCK, &block_sigint, NULL);
}

void block_sigchld(void)
{
    sigset_t block_sigchld;
    sigemptyset(&block_sigchld);
    sigaddset(&block_sigchld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_sigchld, NULL);
}

void unblock_sigint(void)
{
    sigset_t ublock_sigint;
    sigemptyset(&ublock_sigint);
    sigaddset(&ublock_sigint, SIGINT);
    sigprocmask(SIG_UNBLOCK, &ublock_sigint, NULL);
}

void unblock_sigchld(void)
{
    sigset_t unblock_sigchld;
    sigemptyset(&unblock_sigchld);
    sigaddset(&unblock_sigchld, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &unblock_sigchld, NULL);
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
    default_action.sa_flags = 0;

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
           sigaction(SIGTTIN, &default_action, NULL) < 0
           || sigaction(SIGTTOU, &default_action, NULL) < 0
           || sigaction(SIGTSTP, &default_action, NULL) < 0
           || sigaction(SIGQUIT, &default_action, NULL) < 0))
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
