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

#include "acommon.h"
#include "aconf.h"
#include "aasync.h"
#include "ashell.h"
#include "aalloc.h"
#include "aalloc.h"
#include "auserstr.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#include <stdio.h>
#include <signal.h>

/* global shell */
struct a_shell ashe = { 0 };

ASHE_PRIVATE inline void ashe_free_ccharp(void *ptr)
{
	ashe_free(*(char *const *)ptr);
}

ASHE_PUBLIC void a_shell_clear_strings(struct a_shell *sh)
{
	a_arr_ccharp_free(&sh->sh_strings, ashe_free_ccharp);
	a_arr_ccharp_init(&sh->sh_strings);
}

ASHE_PUBLIC void a_shell_clear_ast(struct a_shell *sh)
{
	a_block_free(&sh->sh_block);
	a_block_init(&sh->sh_block);
}

ASHE_PRIVATE void sh_init_vars(struct a_shell *sh)
{
	a_arr_char *pidbuf;

	/* set status */
	ashe_setenv(ASHE_VAR_STATUS, "0", 1);
	/* set PID */
	pidbuf = &sh->sh_status;
	a_arr_char_push_number(pidbuf, getpid());
	a_arr_char_push(pidbuf, '\0');
	ashe_setenv(ASHE_VAR_PID, a_arrp_ptr(pidbuf), 1);
	a_arrp_len(pidbuf) = 0;
}

ASHE_PUBLIC void a_shell_init(struct a_shell *sh)
{
	pid_t sh_pgid;

	memset(sh, 0, sizeof(struct a_shell));
	sh_pgid = ashe_getpgrp();
	a_arr_ccharp_init_cap(&sh->sh_strings, 8);
	a_arr_char_init_cap(&sh->sh_status, 8);
	a_arr_char_init_cap(&sh->sh_welcome, sizeof(ASHE_WELCOME));
	sh_init_vars(sh);

get_terminal:
	if (!(sh->sh_flags.interactive = isatty(STDIN_FILENO))) {
		/*
		 * TODO: given argument '-c' allow shell
		 * to execute commands or '-s' to check for
		 * syntax while not being interactive, but
		 * make sure to disable job control.
		 */
		ashe_kill(sh_pgid, SIGTTIN);
		goto get_terminal;
	}

	while (tcgetpgrp(STDIN_FILENO) != (sh_pgid = ashe_getpgrp()))
		ashe_kill(sh_pgid, SIGTTIN);
	ashe_setpgid(getpid(), sh_pgid);
	ashe_tcsetpgrp(sh_pgid);

	a_jobcntl_init(&sh->sh_jobcntl);
	a_term_init();
	ashe_init_sighandlers();
	ashe_pwelcome();
}

ASHE_PUBLIC void a_shell_free(struct a_shell *sh)
{
	a_jobcntl_free(&sh->sh_jobcntl);
	a_term_free();
	a_arr_ccharp_free(&sh->sh_strings, ashe_free_ccharp);
	a_arr_char_free(&sh->sh_welcome, NULL);
	a_arr_char_free(&sh->sh_status, NULL);
	a_block_free(&sh->sh_block);
}
