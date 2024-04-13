#include "acommon.h"
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

ASHE_PRIVATE void sh_init_buffers(struct a_shell *sh)
{
	a_arr_ccharp_init(&sh->sh_buffers);
	a_arr_char_init_cap(&sh->sh_prompt, sizeof(ASHE_PROMPT));
	a_arr_char_init_cap(&sh->sh_welcome, sizeof(ASHE_WELCOME));
	a_arr_char_init_cap(&sh->sh_status, 8);
}

ASHE_PRIVATE void sh_init_flags(struct a_shell *sh)
{
	memset(&sh->sh_flags, 0, sizeof(struct a_flags));
}

ASHE_PRIVATE void sh_init_settings(struct a_shell *sh)
{
	memset(&sh->sh_settings, 0, sizeof(struct a_settings));
}

ASHE_PRIVATE void sh_init_vars(struct a_shell *sh)
{
	a_arr_char *pidbuf;

	ashe_setenv(ASHE_VAR_STATUS, "0", 1);
	/* use status buffer to store PID temporarily */
	pidbuf = &sh->sh_status;
	a_arr_char_push_num(pidbuf, getpid());
	a_arr_char_push(pidbuf, '\0');
	ashe_setenv(ASHE_VAR_PID, a_arrp_ptr(pidbuf), 1);
	a_arrp_len(pidbuf) = 0;
}

ASHE_PUBLIC void a_shell_init(struct a_shell *sh)
{
	pid_t sh_pgid;

#if defined(ASHE_DBG_CURSOR)
	logfile_create("debug_cursor.dbg.txt", ALOG_CURSOR);
#endif
#if defined(ASHE_DBG_LINES)
	logfile_create("debug_lines.dbg.txt", ALOG_LINES);
#endif
	sh_pgid = getpgrp();
	a_jobcntl_init(&sh->sh_jobcntl);
	sh_init_buffers(sh);
	sh_init_flags(sh);
	sh_init_settings(sh);
	sh_init_vars(sh);

get_terminal:
	sh->sh_flags.interactive = isatty(STDIN_FILENO);
	if (sh->sh_flags.interactive) {
		while (tcgetpgrp(STDIN_FILENO) != (sh_pgid = getpgrp()))
			ashe_kill(sh_pgid, SIGTTIN);
		ashe_setpgid(getpid(), sh_pgid);
		ashe_tcsetpgrp(sh_pgid);

		a_term_init();
		ashe_init_sighandlers();
		ashe_pwelcome();
		return;
	}
	/*
	 * TODO: given argument '-c' allow shell
	 * to execute commands or '-s' to check for
	 * syntax while not being interactive, but
	 * make sure to disable job control.
	 */
	ashe_kill(sh_pgid, SIGTTIN);
	goto get_terminal;
}

ASHE_PUBLIC void ashe_free_ccharp(void *ptr)
{
	ashe_free(*(char *const *)ptr);
}

ASHE_PUBLIC void a_shell_free(struct a_shell *sh)
{
	a_jobcntl_free(&sh->sh_jobcntl);
	a_term_free();
	a_arr_ccharp_free(&sh->sh_buffers, ashe_free_ccharp);
	a_arr_char_free(&sh->sh_prompt, NULL);
	a_arr_char_free(&sh->sh_welcome, NULL);
	a_arr_char_free(&sh->sh_status, NULL);
	a_block_free(&sh->sh_block);
}
