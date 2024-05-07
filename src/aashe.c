#include "aasync.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "aparser.h"
#include "ashell.h"
#include "arun.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#define REPL for (;; ashe_dprint("REP[L]"))

/* ashe entry */
int main(int argc, char **argv)
{
	/* TODO: use args */
	ASHE_UNUSED(argc);
	ASHE_UNUSED(argv);
	struct a_jobcntl *jobcntl;
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
		ashe_dprintf("read %n bytes: '%s'", A_IBF.len, A_IBF.data);
		a_shell_clear_ast(&ashe);
		a_shell_clear_strings(&ashe);
		ashe_expandvars(&A_IBF); /* '$' */
		ashe_dprintf("expanded '$' vars: '%s'", A_IBF.data);

		if (a_arr_len(A_IBF) <= 1 || (status = ashe_parse(a_arr_ptr(A_IBF))) == 1) {
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
		ashe_dprintf("storing status '%s' into '%s' variable", a_arrp_ptr(statusbuf),
			     ASHE_VAR_STATUS);
		if (ASHE_UNLIKELY(setenv(ASHE_VAR_STATUS, a_arr_ptr(ashe.sh_status), 1) < 0))
			ashe_panic_libcall(setenv);
		a_arr_len(ashe.sh_status) = 0;
	}
}
