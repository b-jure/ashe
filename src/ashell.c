#include "aconf.h"
#include "aasync.h"
#include "ashell.h"
#include "aalloc.h"
#include "aprompt.h"

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
	pid_t sh_pgid;
	ubyte shell_is_interactive = isatty(STDIN_FILENO);

	if (shell_is_interactive) {
		while (tcgetpgrp(STDIN_FILENO) != (sh_pgid = getpgrp()))
			if (unlikely(kill(-sh_pgid, SIGTTIN) < 0))
				goto error;
		if (unlikely(setpgid(getpid(), sh_pgid) < 0))
			goto error;
		if (unlikely(tcsetpgrp(STDIN_FILENO, sh_pgid) < 0))
			goto error;
		init_vars();
		init_signal_handlers();
		JobControl_init(&sh->sh_jobcntl);
		Terminal_init(&sh->sh_term);
		print_welcome();
	}
	return;
error:
	print_errno();
	panic(NULL);
}
