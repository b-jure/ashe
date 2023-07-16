#include "ashe_string.h"
#include "ashe_utils.h"
#include "parser.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define PROMPT "/ashe/::> "
#define INSIZE 200

static void unescape(string_t *string, byte *start);

int rcmdline(string_t *buffer)
{
    byte input[INSIZE];
    byte *ptr;
    byte *lastparen;

    ptr = lastparen = string_slice(buffer, 0);
    bool parens = false;

    while(fgets(input, INSIZE, stdin) != NULL) {
        if(string_len(buffer) + strlen(input) >= ARG_MAX) {
            CMDLINE_ARGSIZE_ERR(ARG_MAX);
            return FAILURE;
        } else if(!string_append(buffer, input, strlen(input))) {
            return FAILURE;
        } else {
            while(is_some((ptr = strchr(ptr, '"')))) {
                if(!parens) {
                    *ptr = NULL_TERM;
                    unescape(buffer, lastparen + 1);
                    *--ptr = '"';
                    parens ^= true;
                    lastparen = ptr++;
                    assert(*lastparen == '"');
                } else if(char_before_ptr(ptr) == '\\') {
                    string_remove_at_ptr(buffer, ptr - 1);
                    string_set_at_ptr(buffer, ptr - 1, '"');
                    assert(*(ptr - 1) == '"');
                } else {
                    parens ^= true;
                    lastparen = ptr++;
                }
            }
            if(!parens && is_some(strchr(lastparen, '\n'))) {
                break;
            }
            ptr = lastparen + 1;
        }
    }

    if(ferror(stdin) != 0) {
        /// Don't exit with failure just warn
        CMDLINE_READ_ERR;
    }

    printf("got input: '%s'\n", input);
    return SUCCESS;
}

static void unescape(string_t *string, byte *start)
{
    byte *escape;
    int c;

    while(is_some(escape = strchr(start, '\\'))) {
        c = char_after_ptr(escape);
        string_remove_at_ptr(string, escape--);
        switch(c) {
            case 't':
                string_set_at_ptr(string, escape, '\t');
                break;
            case 'n':
                string_set_at_ptr(string, escape, '\n');
                break;
            case 'r':
                string_set_at_ptr(string, escape, '\r');
                break;
            case 'b':
                string_set_at_ptr(string, escape, '\b');
                break;
            case 'f':
                string_set_at_ptr(string, escape, '\f');
                break;
            case 'v':
                string_set_at_ptr(string, escape, '\v');
                break;
            case '0':
                string_set_at_ptr(string, escape, '\0');
                break;
            case '\'':
                string_set_at_ptr(string, escape, '\'');
                break;
            case '"':
                string_set_at_ptr(string, escape, '"');
                break;
            case '\\':
                string_set_at_ptr(string, escape, '\\');
                break;
            default:
                break;
        }
    }
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
