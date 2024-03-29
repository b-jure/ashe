#include "aconf.h"
#include "aasync.h"
#include "ashell.h"
#include "aalloc.h"
#include "aprompt.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#include <stdio.h>
#include <signal.h>

/* global shell */
Shell ashe = { 0 };

static int32 init_vars(void)
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

void Shell_init(Shell *sh)
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
	memset(&sh->sh_settings, 0, sizeof(Settings));
	memset(&sh->sh_flags, 0, sizeof(Flags));
	init_vars();
try_again:
	sh->sh_flags.interactive = isatty(STDIN_FILENO);
	if (sh->sh_flags.interactive) {
		Terminal_init(&sh->sh_term);
		while (tcgetpgrp(STDIN_FILENO) != (sh_pgid = getpgrp()))
			if (unlikely(kill(-sh_pgid, SIGTTIN) < 0))
				goto error;
		if (unlikely(setpgid(getpid(), sh_pgid) < 0))
			goto error;
		if (unlikely(tcsetpgrp(STDIN_FILENO, sh_pgid) < 0))
			goto error;
		init_signal_handlers();
		print_welcome();
	} else {
		if (unlikely(kill(-sh_pgid, SIGTTIN) < 0))
			goto error;
		goto try_again;
	}
	return;
error:
	print_errno();
	panic(NULL);
}
