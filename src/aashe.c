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

#define clear_buffers()                                            \
	do {                                                       \
		a_arr_ccharp_free(&ashe.sh_buffers, wafree_charp); \
		a_arr_ccharp_init(&ashe.sh_buffers);               \
	} while (0)

/* ashe */
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
		prompt_print();
		a_terminput_clear();
		a_jobcntl_update_and_notify(jobcntl);
		enable_async_jobcntl_updates();
		a_terminput_read();
		ashe_dprintf("read %u bytes: '%s'", IBF.len, IBF.data);
		disable_async_jobcntl_updates();
		a_jobcntl_update_and_notify(jobcntl);
		clear_ast();
		clear_buffers();
		expand_vars(&IBF); /* '$' */
		ashe_dprintf("expanded '$' vars: '%s'", IBF.data);
		if (IBF.len <= 1 || a_parse_block(IBF.data) < 0)
			continue;
#ifdef ASHE_DBG_AST
		debug_ast(&ashe.sh_block);
#endif
		status = a_run_block(&ashe.sh_block);
		status = (status < 0 ? -status : status);
		if (unlikely(snprintf(retstatus, INT_DIGITS, "%d", status) < 0))
			goto error;
		ashe_dprintf("storing status '%s' into '%s' variable",
			     retstatus, ASHE_VAR_STATUS);
		if (unlikely(setenv(ASHE_VAR_STATUS, retstatus, 1)))
			goto error;
	}
error:
	ashe_perrno();
	panic(NULL);
}
