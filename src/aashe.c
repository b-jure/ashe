#include "aasync.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "aparser.h"
#include "ashell.h"
#include "arun.h"
#include "aprompt.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#include <stdio.h>

#define clear_ast()                           \
	do {                                  \
		a_block_free(&ashe.sh_block); \
		a_block_init(&ashe.sh_block); \
	} while (0)

#define clear_buffers()                                                \
	do {                                                           \
		a_arr_ccharp_free(&ashe.sh_buffers, ashe_free_ccharp); \
		a_arr_ccharp_init(&ashe.sh_buffers);                   \
	} while (0)

/* ashe entry */
int main(int argc, char **argv)
{
	unused(argc);
	unused(argv);
	char retstatus[INT_DIGITS];
	int32 status = 0;
	struct a_jobcntl *jobcntl;

	jobcntl = &ashe.sh_jobcntl;
	a_shell_init(&ashe);
	ashe_dprintf("shell is running %s",
		     (ashe.sh_flags.interactive ? "interactively" :
						  "in background"));
	for (;;) {
		ashe_pprompt();
		a_terminput_clear();
		a_jobcntl_update_and_notify(jobcntl);
		ashe_enable_jobcntl_updates();
		a_terminput_read();
		ashe_dprintf("read %u bytes: '%s'", A_IBF.len, A_IBF.data);
		ashe_disable_jobcntl_updates();
		a_jobcntl_update_and_notify(jobcntl);
		clear_ast();
		clear_buffers();
		ashe_expandvars(&A_IBF); /* '$' */
		ashe_dprintf("expanded '$' vars: '%s'", A_IBF.data);
		if (A_IBF.len <= 1 || a_parse_block(A_IBF.data) < 0)
			continue;
#ifdef ASHE_DBG_AST
		debug_ast(&ashe.sh_block);
#endif
		status = a_run_block(&ashe.sh_block);
		status = (status < 0 ? -status : status);
		if (unlikely(snprintf(retstatus, INT_DIGITS, "%d", status) <
			     0)) {
			ashe_perrno("snprintf");
			goto panic;
		}
		ashe_dprintf("storing status '%s' into '%s' variable",
			     retstatus, ASHE_VAR_STATUS);
		if (unlikely(setenv(ASHE_VAR_STATUS, retstatus, 1))) {
			ashe_perrno("setenv");
			goto panic;
		}
	}
panic:
	ashe_panic(NULL);
}
