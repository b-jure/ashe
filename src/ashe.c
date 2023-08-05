#include "ashe_string.h"
#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"
#include "parser.h"
#include "shell.h"

#include <string.h>
#include <termios.h>

#define INT_DIGITS 10

int main()
{
    int status = SUCCESS;

    /* Initialize the shell global */
    shell_init();

    inbuff_t *input_buffer = &shell.sh_term.tm_inbuff;
    commandline_t *cmdline = &shell.sh_cmdline;

    while(true) {
        ATOMIC_PRINT(pprompt());

        enable_async_joblist_update();
        try_wait_missed_sigchld_signals();

        inbuff_clear(input_buffer);
        read_input(input_buffer);

        try_wait_missed_sigchld_signals();
        disable_async_joblist_update();

        commandline_clear(cmdline);
        if(input_buffer->in_len <= 1
           || parse_commandline(input_buffer->in_buffer, cmdline) == FAILURE)
            continue;

        commandline_execute(cmdline, &status);
        byte retstat[INT_DIGITS + 2];
        sprintf(retstat, "%d", status);
        setenv("?", retstat, 1);
    }
}
