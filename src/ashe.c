#include "ashe_string.h"
#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"
#include "parser.h"
#include "shell.h"

#include <string.h>
#include <termios.h>

int main()
{
    int status = SUCCESS;

    /* Initialize the shell global */
    shell_init();

    while(true) {
        ATOMIC_PRINT(pprompt());

        enable_async_joblist_update();
        try_wait_missed_sigchld_signals();

        inbuff_clear(&inbuff);
        read_input(&inbuff);

        try_wait_missed_sigchld_signals();
        disable_async_joblist_update();

        commandline_clear(&cmdline);
        if(inbuff.in_len <= 1 || parse_commandline(inbuff.in_buffer, &cmdline) == FAILURE)
            continue;

        commandline_execute(&cmdline, &status);
        byte retstat[INT_DIGITS + 2];
        sprintf(retstat, "%d", status);
        setenv("?", retstat, 1);
    }
}
