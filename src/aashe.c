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
		ArrayCharptr_free(&ashe.sh_buffers, wafree_charp); \
		ArrayCharptr_init(&ashe.sh_buffers);               \
		ArrayConditional_free(&ashe.sh_conds,              \
				      (FreeFn)Conditional_free);   \
		ArrayConditional_init(&ashe.sh_conds);             \
	} while (0)

/* ashe */
int main(int argc, char **argv)
{
	unused(argc);
	unused(argv);
	int32 status = 0;
	char retstatus[INT_DIGITS];
	JobControl *jobcntl = &ashe.sh_jobcntl;

	Shell_init(&ashe);

	for (;;) {
		prompt_print();
		TerminalInput_clear();
		JobControl_update_and_notify(jobcntl);
		enable_async_jobcntl_updates();
		TerminalInput_read();
		disable_async_jobcntl_updates();
		JobControl_update_and_notify(jobcntl);
		clear_parsed_data();
		if (IBF.len <= 1 || ashe_parse(IBF.data) < 0)
			continue;
		status = cmdexec(&ashe.sh_conds);
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
