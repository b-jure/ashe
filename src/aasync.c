#include "aasync.h"
#include "acommon.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "ashell.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

/* Signals which we handle and block/unblock. */
static const int32 signals[] = {
	SIGINT,
	SIGCHLD,
	SIGWINCH,
};

ASHE_PRIVATE void SIGINT_handler(int signum);
ASHE_PRIVATE void SIGCHLD_handler(int signum);
ASHE_PRIVATE void SIGWINCH_handler(int signum);

static sighandler handlers[] = {
	SIGINT_handler,
	SIGCHLD_handler,
	SIGWINCH_handler,
};

/* Number of elements in 'signals' and signal handlers. */
#define SIGN (sizeof(signals) / sizeof(signals[0]))

/* Mask signal 'signum' by specifying 'how'.
 * 'how' should be SIG_BLOCK or SIG_UNBLOCK. */
ASHE_PRIVATE void mask_signal(int signum, int how)
{
	sigset_t signal;
	sigemptyset(&signal);
	sigaddset(&signal, signum);
	sigprocmask(how, &signal, NULL);
}

/* SIGWINCH signal handler */
ASHE_PRIVATE void SIGWINCH_handler(int signum)
{
	unused(signum);
	mask_signal(SIGCHLD, SIG_BLOCK);
	mask_signal(SIGINT, SIG_BLOCK);
	ashe.sh_flags.interrupt = 1;
	get_winsize_or_panic(&ashe.sh_term.tm_rows, &ashe.sh_term.tm_columns);
	mask_signal(SIGCHLD, SIG_UNBLOCK);
	mask_signal(SIGINT, SIG_UNBLOCK);
}

/* SIGINT signal handler */
ASHE_PRIVATE void SIGINT_handler(int signum)
{
	unused(signum);
	mask_signal(SIGWINCH, SIG_BLOCK);
	mask_signal(SIGCHLD, SIG_BLOCK);
	ashe.sh_flags.interrupt = 1;
	TerminalInput_gotoend(&ashe.sh_term.tm_input);
	fprintf(stderr, "\r\n");
	print_prompt();
	TerminalInput_clear(&ashe.sh_term.tm_input);
	mask_signal(SIGWINCH, SIG_UNBLOCK);
	mask_signal(SIGCHLD, SIG_UNBLOCK);
}

/* SIGCHLD signal handler */
ASHE_PRIVATE void SIGCHLD_handler(int signum)
{
	mask_signal(SIGWINCH, SIG_BLOCK);
	mask_signal(SIGINT, SIG_BLOCK);
	ashe.sh_flags.interrupt = 1;
	if (unlikely(get_cursor_pos(NULL, &ashe.sh_term.tm_col)))
		panic("couldn't get cursor position.");
	JobControl_update_and_notify(&ashe.sh_jobcntl);
	mask_signal(SIGWINCH, SIG_UNBLOCK);
	mask_signal(SIGINT, SIG_UNBLOCK);
}

/* Masks signals in 'signals' array. */
ASHE_PUBLIC void ashe_mask_signals(int32 how)
{
	for (int32 i = 0; i < SIGN; i++)
		mask_signal(signals[i], how);
}

/* Enables asynchronous 'JobControl' updates. */
ASHE_PUBLIC void enable_async_jobcntl_updates(void)
{
	struct sigaction old_action;
	sigaction(SIGCHLD, NULL, &old_action);
	old_action.sa_handler = SIGCHLD_handler;
	sigaction(SIGCHLD, &old_action, NULL);
}

/* disables asynchronous 'JobControl' updates. */
ASHE_PUBLIC void disable_async_jobcntl_updates(void)
{
	struct sigaction old_action;
	sigaction(SIGCHLD, NULL, &old_action);
	old_action.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &old_action, NULL);
}

/* Initializes signal handlers. */
ASHE_PUBLIC int32 init_signal_handlers(void)
{
	struct sigaction default_action;
	sigemptyset(&default_action.sa_mask);

	for (int32 i = 0; i < SIGN; i++) {
		sighandler handler = handlers[i];
		if (signals[i] == SIGCHLD)
			handler = SIG_DFL;
		default_action.sa_handler = handler;
		default_action.sa_flags = 0;
		if (unlikely(sigaction(signals[i], &default_action, NULL) < 0))
			goto l_error;
	}

	default_action.sa_handler = SIG_IGN;
	if (unlikely(sigaction(SIGTTIN, &default_action, NULL) < 0 ||
		     sigaction(SIGTTOU, &default_action, NULL) < 0 ||
		     sigaction(SIGTSTP, &default_action, NULL) < 0 ||
		     sigaction(SIGQUIT, &default_action, NULL) < 0)) {
l_error:
		print_errno();
		return -1;
	}

	return 0;
}
