#include "async.h"
#include "input.h"
#include "parser.h"
#include "shell.h"

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

        Commandline_free(&cmdline);
        if(inbuff.in_len <= 1 || parse_commandline(inbuff.in_buffer, &cmdline) == FAILURE) {
            continue;
        }

        Commandline_execute(&cmdline, &status);
        byte retstat[UINT_DIGITS + 2];
        sprintf(retstat, "%d", status);
        setenv("?", retstat, 1);
    }
}
