#include "ashe_string.h"
#include "ashe_utils.h"
#include "parser.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define PROMPT "/ashe/::> "
#define INSIZE 200

int rcmdline(string_t *buffer)
{
    byte input[INSIZE];
    byte *ptr;
    size_t appended;
    bool dq = false;

    while(fgets(input, INSIZE, stdin) != NULL) {
        if(string_len(buffer) + strlen(input) >= ARG_MAX) {
            CMDLINE_ARGSIZE_ERR(ARG_MAX);
            return FAILURE;
        } else if(!string_append(buffer, input, (appended = strlen(input)))) {
            return FAILURE;
        } else {
            ptr = string_slice(buffer, string_len(buffer) - appended);
            while(is_some((ptr = strchr(ptr, '"')))) {
                if(char_before_ptr(ptr) != '\\') {
                    dq ^= true;
                }
                ptr++;
            }
            if(!dq && string_last(buffer) == '\n') {
                break;
            }
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
    bool set_env = false;

    if(is_null(line = string_with_cap(INSIZE))) {
        exit(EXIT_FAILURE);
    }

    if(is_null((cmdline = commandline_new()).conditionals)) {
        string_drop(line);
        exit(EXIT_FAILURE);
    }

    while(true) {
        printf("%s", PROMPT);
        string_clear(line);

        if(rcmdline(line) == FAILURE) {
            string_drop(line);
            commandline_drop(&cmdline);
            exit(EXIT_FAILURE);
        }

        printf("Got commandline ---> '%s'\n", string_ref(line));

        if(string_len(line) == 1
           || parse_commandline(string_ref(line), &cmdline, &set_env) == FAILURE)
        {
            printf("\nErrored while parsing commandline\n");
            continue;
        }

        if(!set_env) {
            printf("THERE ARE COMMANDS TO BE EXECUTED\n");
            commandline_execute(&cmdline, &status);
        }
        printf("Finished executing commandline with status: %d\n", status);
        commandline_clear(&cmdline);
    }
}
