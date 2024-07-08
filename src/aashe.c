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

#include "aasync.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "aparser.h"
#include "ashell.h"
#include "arun.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#define REPL for (;;)

/* ashe entry */
int main(int argc, char **argv)
{
	/* TODO: use args */
	ASHE_UNUSED(argc);
	ASHE_UNUSED(argv);
	struct a_jobcntl *jobcntl;
	const char *cmd;
	a_arr_char *statusbuf;
	a_int32 status;

	a_shell_init(&ashe);
	statusbuf = &ashe.sh_status;
	jobcntl = &ashe.sh_jobcntl;
	status = 0;

#if defined(ASHE_DBG_CURSOR)
	logfile_create("/tmp/ashe.cursor.dbg.txt", ALOG_CURSOR);
#endif
#if defined(ASHE_DBG_LINES)
	logfile_create("/tmp/ashe.lines.dbg.txt", ALOG_LINES);
#endif

	REPL
	{
		a_jobcntl_update_and_notify(jobcntl);
		ashe_enable_jobcntl_updates();
		a_term_read();
		ashe_disable_jobcntl_updates();
		a_shell_clear_ast(&ashe);
		a_shell_clear_strings(&ashe);
		ashe_expandvars(&A_IBF);

		if (a_arr_len(A_IBF) <= 1)
			continue;

		cmd = ashe_dupstrn(a_arr_ptr(A_IBF), a_arr_len(A_IBF) - 1);
		ashe_newhisthead(&ashe.sh_history, cmd);

		if ((status = ashe_parse(a_arr_ptr(A_IBF))) == 1) {
			continue;
		} else if (status < 0) {
			a_arr_char_push_str(statusbuf, "1", 2);
			goto setenv;
		}

#if defined(ASHE_DBG_AST) && defined(ASHE_DBG)
		debug_ast(&ashe.sh_block);
#endif

		status = abs(ashe_run(&ashe.sh_block));
		a_arr_char_push_number(statusbuf, status);
		a_arr_char_push(statusbuf, '\0');

setenv:
		if (a_unlikely(setenv(ASHE_VAR_STATUS, a_arr_ptr(ashe.sh_status), 1) < 0))
			ashe_panic_libcall(setenv);
		a_arr_len(ashe.sh_status) = 0;
	}
}
