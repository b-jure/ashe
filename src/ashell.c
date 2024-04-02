#include "aconf.h"
#include "aasync.h"
#include "ashell.h"
#include "aalloc.h"
#include "aalloc.h"
#include "aprompt.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#include <stdio.h>
#include <signal.h>

/* global shell */
Shell ashe = { 0 };

static int32 init_ashe_vars(void)
{
	char buffer[INT_DIGITS + 1];

	if (unlikely(setenv(ASHE_VAR_STATUS, "0", 1) < 0))
		return -1;
	if (unlikely(snprintf(buffer, INT_DIGITS, "%d", getpid()) < 0))
		return -1;
	if (unlikely(setenv(ASHE_VAR_PID, buffer, 1) < 0))
		return -1;
	return 0;
}

/*
 * TODO: in future allow shell to not be interactive but
 * only with certian arguments (such as '-c'). 
 */
ASHE_PUBLIC void Shell_init(Shell *sh)
{
	pid_t sh_pgid = getpgrp();

#ifdef ASHE_DBG_CURSOR
	logfile_create("debug_cursor.dbg.txt", ALOG_CURSOR);
#endif
#ifdef ASHE_DBG_LINES
	logfile_create("debug_lines.dbg.txt", ALOG_LINES);
#endif
	JobControl_init(&sh->sh_jobcntl);
	ArrayCharptr_init(&sh->sh_buffers);
	ArrayConditional_init(&sh->sh_conds);
	Buffer_init_cap(&sh->sh_prompt, sizeof(ASHE_PROMPT));
	Buffer_init_cap(&sh->sh_welcome, sizeof(ASHE_WELCOME));
	memset(&sh->sh_settings, 0, sizeof(Settings));
	memset(&sh->sh_flags, 0, sizeof(Flags));
	init_ashe_vars();
get_terminal:
	sh->sh_flags.interactive = isatty(STDIN_FILENO);
	if (sh->sh_flags.interactive) {
		Terminal_init();
		while (tcgetpgrp(STDIN_FILENO) != (sh_pgid = getpgrp()))
			if (unlikely(kill(-sh_pgid, SIGTTIN) < 0))
				goto error;
		if (unlikely(setpgid(getpid(), sh_pgid) < 0))
			goto error;
		if (unlikely(tcsetpgrp(STDIN_FILENO, sh_pgid) < 0))
			goto error;
		init_signal_handlers();
		welcome_print();
	} else {
		if (unlikely(kill(-sh_pgid, SIGTTIN) < 0))
			goto error;
		goto get_terminal;
	}
	return;
error:
	ashe_perrno();
	panic(NULL);
}

ASHE_PUBLIC void wafree_charp(void *ptr)
{
	afree(*(char **)ptr);
}

ASHE_PUBLIC void Shell_free(Shell *sh)
{
	JobControl_free(&sh->sh_jobcntl);
	Terminal_free();
	ArrayCharptr_free(&sh->sh_buffers, wafree_charp);
	Buffer_free(&sh->sh_prompt, NULL);
	Buffer_free(&sh->sh_welcome, NULL);
	ArrayConditional_free(&sh->sh_conds, (FreeFn)Conditional_free);
}
