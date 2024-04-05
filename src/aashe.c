#include "aasync.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "aparser.h"
#include "ashell.h"
#include "arun.h"
#include "aprompt.h"

#include <stdio.h>

#define clear_parsed_data()                                        \
	do {                                                       \
		a_block_free(&ashe.sh_block);                      \
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

	for (;;) {
		prompt_print();
		TerminalInput_clear();
		a_jobcntl_update_and_notify(jobcntl);
		enable_async_jobcntl_updates();
		TerminalInput_read();
		disable_async_jobcntl_updates();
		a_jobcntl_update_and_notify(jobcntl);
		clear_parsed_data();
		expand_vars(&IBF); /* '$' */
		if (IBF.len <= 1 || ashe_block(IBF.data) < 0)
			continue;
		status = cmdexec(&ashe.sh_block);
		status = (status < 0 ? -status : status);
		if (unlikely(snprintf(retstatus, INT_DIGITS, "%d", status) < 0))
			goto error;
		if (unlikely(setenv(ASHE_VAR_STATUS, retstatus, 1)))
			goto error;
	}
error:
	ashe_perrno();
	panic(NULL);
}
