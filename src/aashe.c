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

#define clear_parsed_data() \
	do {                \
	} while (0)

/* ashe */
int main(int argc, char **argv)
{
	unused(argc);
	unused(argv);
	char retstatus[INT_DIGITS];
	int32 status = 0;
	struct a_jobcntl *jobcntl;
#ifdef ASHE_DBG_AST
	a_arr_char out;
#endif

	jobcntl = &ashe.sh_jobcntl;
	a_shell_init(&ashe);
#ifdef ASHE_DBG_MAIN
	ashe_dprintf("shell is running %s",
		     (ashe.sh_flags.interactive ? "interactively" :
						  "in background"));
#endif
	for (;;) {
		prompt_print();
		a_terminput_clear();
		a_jobcntl_update_and_notify(jobcntl);
		enable_async_jobcntl_updates();
		a_terminput_read();
#ifdef ASHE_DBG_MAIN
		ashe_dprintf("read %u bytes", IBF.len);
		ashe_dprintf("'%s'", IBF.data);
#endif
		disable_async_jobcntl_updates();
		a_jobcntl_update_and_notify(jobcntl);
		a_block_free(&ashe.sh_block);
		a_block_init(&ashe.sh_block);
		a_arr_ccharp_free(&ashe.sh_buffers, wafree_charp);
		a_arr_ccharp_init(&ashe.sh_buffers);
		expand_vars(&IBF); /* '$' */
#ifdef ASHE_DBG_MAIN
		ashe_dprintf("expanded '$' vars");
		ashe_dprintf("'%s'", IBF.data);
#endif
		if (IBF.len <= 1 || a_parse_block(IBF.data) < 0) {
			continue;
		}
#ifdef ASHE_DBG_AST
		a_arr_char_init(&out);
		debug_block(&ashe.sh_block, NULL, 0, &out);
		a_arr_char_push(&out, '\0');
		ashe_dprintf("dumping AST...\n'''\n%s\n'''", out.data);
		a_arr_char_free(&out, NULL);
#endif
		status = a_run_block(&ashe.sh_block);
		status = (status < 0 ? -status : status);
		if (unlikely(snprintf(retstatus, INT_DIGITS, "%d", status) < 0))
			goto error;
#ifdef ASHE_DBG_MAIN
		ashe_dprintf("storing status '%s' into '%s' variable",
			     retstatus, ASHE_VAR_STATUS);
#endif
		if (unlikely(setenv(ASHE_VAR_STATUS, retstatus, 1)))
			goto error;
	}
error:
	ashe_perrno();
	panic(NULL);
}
