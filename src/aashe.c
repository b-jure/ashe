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

// clang-format off
/* ashe entry */
int main(int argc, char **argv)
{
	ASHE_UNUSED(argc);
	ASHE_UNUSED(argv);
	a_arr_char retstatus;
	struct a_jobcntl *jobcntl;
	a_int32 status;

	status = 0;
	jobcntl = &ashe.sh_jobcntl;
	a_shell_init(&ashe);
	a_arr_char_init_cap(&retstatus, 16);
	ashe_dprintf("shell is running %s", (ashe.sh_flags.interactive ? "interactively" : "in background"));

	for (;;) { /* REPL */
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
		if (A_IBF.len <= 1 || (status = ashe_parse(a_arr_ptr(A_IBF))) == 1) {
			continue;
		} else if (status < 0) {
			a_arr_char_push_str(&retstatus, "1", 2);
			goto setenv;
		}
#ifdef ASHE_DBG_AST
		debug_ast(&ashe.sh_block);
#endif
		status = abs(ashe_interpret(&ashe.sh_block));
		a_arr_char_push_num(&retstatus, status);
		a_arr_char_push(&retstatus, '\0');
setenv:
		ashe_dprintf("storing status '%s' into '%s' variable", a_arr_ptr(retstatus), ASHE_VAR_STATUS);
		if (ASHE_UNLIKELY(setenv(ASHE_VAR_STATUS, a_arr_ptr(retstatus), 1) < 0)) {
			ashe_perrno("setenv");
			goto panic;
		}
		a_arr_len(retstatus) = 0;
	}
panic:
	ashe_panic(NULL);
}
// clang-format on
