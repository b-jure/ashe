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
	int status;

	status = 0;

	if (unlikely(setenv(ASHE_VAR_STATUS, "0", 1) < 0))
		defer(-1);
	if (unlikely(snprintf(buffer, INT_DIGITS, "%d", getpid()) < 0))
		defer(-1);
	if (unlikely(setenv(ASHE_VAR_PID, buffer, 1) < 0))
		defer(-1);
defer:
	return status;
}

/*
 * TODO: in future allow shell to not be interactive but
 * only with certian arguments (such as '-c').
 */
// clang-format off
ASHE_PUBLIC void a_shell_init(struct a_shell *sh)
{
	pid_t sh_pgid;

#ifdef ASHE_DBG_CURSOR
	logfile_create("debug_cursor.dbg.txt", ALOG_CURSOR);
#endif
#ifdef ASHE_DBG_LINES
	logfile_create("debug_lines.dbg.txt", ALOG_LINES);
#endif
	sh_pgid = getpgrp();
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
		a_term_init();
		while (tcgetpgrp(STDIN_FILENO) != (sh_pgid = getpgrp())) {
			if (unlikely(kill(-sh_pgid, SIGTTIN) < 0)) {
				ashe_perrno("can't send signal %d to pgid %d",
					    SIGTTIN, sh_pgid);
				goto panic;
			}
		}
		if (unlikely(setpgid(getpid(), sh_pgid) < 0)) {
			ashe_perrno("can't set shell process group");
			goto panic;
		}
		if (unlikely(tcsetpgrp(STDIN_FILENO, sh_pgid) < 0)) {
			ashe_perrno("can't set terminal foreground process group");
			goto panic;
		}
		ashe_init_sighandlers();
		ashe_pwelcome();
	} else {
		if (unlikely(kill(-sh_pgid, SIGTTIN) < 0)) {
			ashe_perrno("can't send signal %d to pgid %d", SIGTTIN, sh_pgid);
			goto panic;
		}
		goto get_terminal;
	}
	return;
panic:
	ashe_panic(NULL);
}
// clang-format on

ASHE_PUBLIC void ashe_free_ccharp(void *ptr)
{
	afree(*(char *const *)ptr);
}

ASHE_PUBLIC void a_shell_free(struct a_shell *sh)
{
	a_jobcntl_free(&sh->sh_jobcntl);
	a_term_free();
	a_arr_ccharp_free(&sh->sh_buffers, ashe_free_ccharp);
	a_arr_char_free(&sh->sh_prompt, NULL);
	a_arr_char_free(&sh->sh_welcome, NULL);
	a_block_free(&sh->sh_block);
}
