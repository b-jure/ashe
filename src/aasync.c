/* ----------------------------------------------------------------------------------------------
 * Copyright (C) 2023-2024 Jure Bagić
 *
 * This file is part of ashe.
 * ashe is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ashe is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ashe.
 * If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------------------------*/

#include <signal.h>
#include <stdio.h>

#include "aasync.h"
#include "acommon.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "ashell.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

/* Signals which we handle and block/unblock. */
static const a_int32 signals[] = {
	SIGINT,
	SIGCHLD,
	SIGWINCH,
};

ASHE_PRIVATE void SIGINT_handler(int signum);
ASHE_PRIVATE void SIGCHLD_handler(int signum);
ASHE_PRIVATE void SIGWINCH_handler(int signum);

static a_sighandler handlers[] = {
	SIGINT_handler,
	SIGCHLD_handler,
	SIGWINCH_handler,
};

/* Mask signal 'signum' by specifying 'how'.
 * 'how' can be SIG_BLOCK or SIG_UNBLOCK. */
ASHE_PUBLIC void ashe_mask_signal(int signum, int how)
{
	sigset_t signal;

	sigemptyset(&signal);
	sigaddset(&signal, signum);
	/* no way this fails, but check to satisfy OCD */
	if (ASHE_UNLIKELY(sigprocmask(how, &signal, NULL) < 0))
		ashe_panic_libcall(sigprocmask);
}

/* SIGWINCH signal handler */
ASHE_PRIVATE void SIGWINCH_handler(int signum)
{
	ASHE_UNUSED(signum);
	ashe_mask_signals(SIG_BLOCK);
	ashe.sh_int = 1;
	sigwinch_redraw();
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
	ASHE_UNUSED(signum);
	ashe_mask_signals(SIG_BLOCK);
	ashe.sh_int = 1;
	ashe_redraw_prompt();
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
	ASHE_UNUSED(signum);
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
ASHE_PUBLIC void ashe_mask_signals(a_int32 how)
{
	a_uint32 i;

	for (i = 0; i < ASHE_ELEMENTS(signals); i++)
		ashe_mask_signal(signals[i], how);
}

/* Enables asynchronous 'JobControl' updates. */
ASHE_PUBLIC void ashe_enable_jobcntl_updates(void)
{
	struct sigaction old_action;

	sigaction(SIGCHLD, NULL, &old_action);
	old_action.sa_handler = SIGCHLD_handler;
	sigaction(SIGCHLD, &old_action, NULL);
}

/* disables asynchronous 'JobControl' updates. */
ASHE_PUBLIC void ashe_disable_jobcntl_updates(void)
{
	struct sigaction old_action;

	sigaction(SIGCHLD, NULL, &old_action);
	old_action.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &old_action, NULL);
}

// clang-format off
/* Initializes signal handlers. */
ASHE_PUBLIC void ashe_init_sighandlers(void)
{
	a_sighandler handler;
	struct sigaction default_action;
	a_uint32 i;

	sigemptyset(&default_action.sa_mask);
	default_action.sa_flags = 0;

	for (i = 0; i < ASHE_ELEMENTS(signals); i++) {
		handler = handlers[i];
		if (signals[i] == SIGCHLD)
			handler = SIG_DFL;
		default_action.sa_handler = handler;
		ashe_sigaction(signals[i], &default_action, NULL);
	}

	default_action.sa_handler = SIG_IGN;
	ashe_sigaction(SIGTTIN, &default_action, NULL);
	ashe_sigaction(SIGTTOU, &default_action, NULL);
	ashe_sigaction(SIGTSTP, &default_action, NULL);
	ashe_sigaction(SIGQUIT, &default_action, NULL);
}
