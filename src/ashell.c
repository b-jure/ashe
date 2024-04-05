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
struct a_shell ashe = { 0 };

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
ASHE_PUBLIC void a_shell_init(struct a_shell *sh)
{
	pid_t sh_pgid = getpgrp();

#ifdef ASHE_DBG_CURSOR
	logfile_create("debug_cursor.dbg.txt", ALOG_CURSOR);
#endif
#ifdef ASHE_DBG_LINES
	logfile_create("debug_lines.dbg.txt", ALOG_LINES);
#endif
	a_jobcntl_init(&sh->sh_jobcntl);
	a_arr_ccharp_init(&sh->sh_buffers);
	a_arr_char_init_cap(&sh->sh_prompt, sizeof(ASHE_PROMPT));
	a_arr_char_init_cap(&sh->sh_welcome, sizeof(ASHE_WELCOME));
	memset(&sh->sh_settings, 0, sizeof(struct a_settings));
	memset(&sh->sh_flags, 0, sizeof(struct a_flags));
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

ASHE_PUBLIC void a_shell_free(struct a_shell *sh)
{
	a_jobcntl_free(&sh->sh_jobcntl);
	Terminal_free();
	a_arr_ccharp_free(&sh->sh_buffers, wafree_charp);
	a_arr_char_free(&sh->sh_prompt, NULL);
	a_arr_char_free(&sh->sh_welcome, NULL);
	a_block_free(&sh->sh_block);
}
