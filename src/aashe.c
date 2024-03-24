#include "aasync.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "aparser.h"
#include "ashell.h"
#include "arun.h"
#include "aprompt.h"

#include <stdio.h>

static void freetokbuffs(void)
{
	uint i;

	for (i = 2; i < ashe.sh_buffers.len; i++)
		afree(ashe.sh_buffers.data[i]);
	ashe.sh_buffers.len = 2;
}

int main()
{
	int status = 0;
	char retstatus[4];
	JobControl *jobcntl = &ashe.sh_jobcntl;
	TerminalInput *tinput = &ashe.sh_term.tm_input;

	Shell_init(&ashe);

	while (1) {
		print_prompt();
		enable_async_jobcntl_updates();
		JobControl_update_and_notify(jobcntl);
		TerminalInput_clear(tinput);
		TerminalInput_read(tinput);
		JobControl_update_and_notify(jobcntl);
		disable_async_jobcntl_updates();
		ArrayConditional_free(&ashe.sh_conds, (FreeFn)Conditional_free);
		freetokbuffs();
		if (tinput->in_ibf.len <= 1 || parse(tinput->in_ibf.data) == -1)
			continue;
		status = cmdexec(&ashe.sh_conds);
		status = (status < 0 ? -status : status);
		if (unlikely(snprintf(retstatus, 4, "%d", status) < 0))
			goto error;
		if (unlikely(setenv(ASHE_VAR_STATUS, retstatus, 1)))
			goto error;
	}
error:
	print_errno();
	panic(NULL);
}
