#include "asheutils.h"
#include "parser.h"
#include "string.h"
#include <stdlib.h>
#include <string.h>

#define PROMPT "/ashe/::> "
#define INSIZE 200

int rcmdline(string_t *buffer)
{
    byte input[INSIZE];

    while(fgets(input, INSIZE, stdin) != NULL) {
        if(string_len(buffer) + strlen(input) >= ARG_MAX) {
            CMDLINE_ARGSIZE_ERR(ARG_MAX);
            return FAILURE;
        } else if(!string_append(buffer, input, strlen(input))) {
            return FAILURE;
        } else if(string_last(buffer) == '\n') {
            break;
        }
    }

    if(ferror(stdin) != 0) {
        /// Don't exit with failure just warn
        CMDLINE_READ_ERR;
    }

    return SUCCESS;
}

int main()
{
    commandline_t cmdline;
    string_t *line;
    int status = SUCCESS;

    if(is_null(line = string_with_cap(INSIZE))) {
        exit(EXIT_FAILURE);
    }

    if(is_null((cmdline = commandline_new()).conditionals)) {
        string_drop(&line);
        exit(EXIT_FAILURE);
    }

    while(true) {
        printf("%s", PROMPT);
        string_set(line, 0, '\0');

        if(rcmdline(line) == FAILURE) {
            string_drop(&line);
            commandline_drop(&cmdline);
            exit(EXIT_FAILURE);
        }

        if(string_len(line) == 1
           || parse_commandline(string_ref(line), &cmdline) == FAILURE)
        {
            continue;
        }

        commandline_execute(&cmdline, &status);

        commandline_clear(&cmdline);
    }
}
