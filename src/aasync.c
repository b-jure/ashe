#include <signal.h>
#include <stdio.h>

#include "aasync.h"
#include "aprompt.h"
#include "acommon.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "ashell.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

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

/* Mask signal 'signum' by specifying 'how'.
 * 'how' should be SIG_BLOCK or SIG_UNBLOCK. */
ASHE_PUBLIC void ashe_mask_signal(int signum, int how)
{
	sigset_t signal;

	if (unlikely(sigemptyset(&signal) < 0 ||
		     sigaddset(&signal, signum) < 0 ||
		     sigprocmask(how, &signal, NULL) < 0)) {
		ashe_perrno();
		panic(NULL);
	}
}

/* SIGWINCH signal handler */
ASHE_PRIVATE void SIGWINCH_handler(int signum)
{
	unused(signum);
	ashe_mask_signals(SIG_BLOCK);
	ashe.sh_int = 1;
	get_winsize_or_panic(&ashe.sh_term.tm_rows, &TCOLMAX);
	if (unlikely(get_cursor_pos(NULL, &TCOL)))
		panic("couldn't get cursor position.");
#ifdef ASHE_DBG_CURSOR
	debug_cursor();
#endif
#ifdef ASHE_DBG_LINES
	debug_lines();
#endif
	ashe_mask_signals(SIG_UNBLOCK);
}

/* SIGINT signal handler */
ASHE_PRIVATE void SIGINT_handler(int signum)
{
	unused(signum);
	ashe_mask_signals(SIG_BLOCK);
	ashe.sh_int = 1;
	ashe_cursor_end();
	ashe_print("\r\n", stderr);
	prompt_print();
	TerminalInput_clear();
	if (unlikely(get_cursor_pos(NULL, &TCOL)) < 0)
		panic("couldn't get cursor position.");
#ifdef ASHE_DBG_CURSOR
	debug_cursor();
#endif
#ifdef ASHE_DBG_LINES
	debug_lines();
#endif
	ashe_mask_signals(SIG_UNBLOCK);
}

/* SIGCHLD signal handler */
ASHE_PRIVATE void SIGCHLD_handler(int signum)
{
	unused(signum);
	ashe_mask_signals(SIG_BLOCK);
	ashe.sh_int = 1;
	a_jobcntl_update_and_notify(&ashe.sh_jobcntl);
#ifdef ASHE_DBG_CURSOR
	debug_cursor();
#endif
#ifdef ASHE_DBG_LINES
	debug_lines();
#endif
	ashe_mask_signals(SIG_UNBLOCK);
}

/* Masks signals in 'signals' array. */
ASHE_PUBLIC void ashe_mask_signals(int32 how)
{
	for (uint32 i = 0; i < ELEMENTS(signals); i++)
		ashe_mask_signal(signals[i], how);
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
ASHE_PUBLIC void init_signal_handlers(void)
{
	sighandler handler;
	struct sigaction default_action;

	sigemptyset(&default_action.sa_mask);
	default_action.sa_flags = 0;

	for (uint32 i = 0; i < ELEMENTS(signals); i++) {
		handler = handlers[i];
		if (signals[i] == SIGCHLD)
			handler = SIG_DFL;
		default_action.sa_handler = handler;
		if (unlikely(sigaction(signals[i], &default_action, NULL) < 0))
			goto l_error;
	}

	default_action.sa_handler = SIG_IGN;
	if (unlikely(sigaction(SIGTTIN, &default_action, NULL) < 0 ||
		     sigaction(SIGTTOU, &default_action, NULL) < 0 ||
		     sigaction(SIGTSTP, &default_action, NULL) < 0 ||
		     sigaction(SIGQUIT, &default_action, NULL) < 0)) {
l_error:
		ashe_perrno();
		panic(NULL);
	}
}
