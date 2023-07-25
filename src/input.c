#include "ashe_utils.h"
#include "input.h"

#include <stdatomic.h>
#include <string.h>

/// Flag that gets set if the rcmdline routine
/// gets interrupted while it is still reading
/// TODO: Recover terminal input
volatile atomic_bool reading = false;

/// Shell terminal modes
struct termios shell_tmodes;

/// Terminal input buffer
struct inbuff_t {
    byte buffer[ARG_MAX];
    size_t len;
};

void enable_raw_mode(void)
{
    tcgetattr(TERMINAL_FD, &shell_tmodes);
    shell_tmodes.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(TERMINAL_FD, TCSAFLUSH, &shell_tmodes);
}

int read_input(string_t *buffer)
{
    reading = true; /// Set the global

    byte input[INSIZE];
    byte *ptr;
    size_t appended;
    bool dq = false;

read_more:
    while(fgets(input, INSIZE, stdin) != NULL) {
        if(__glibc_unlikely(string_len(buffer) + strlen(input) >= ARG_MAX)) {
            ATOMIC_PRINT({ pwarn("commandline argument size %d exceeded", ARG_MAX); });
            return FAILURE;
        } else if(__glibc_unlikely(
                      !string_append(buffer, input, (appended = strlen(input)))))
        {
            return FAILURE;
        } else {
            ptr = string_slice(buffer, string_len(buffer) - appended);
            while(is_some((ptr = strchr(ptr, '"')))) {
                if(char_before_ptr(ptr) != '\\')
                    dq ^= true;
                ptr++;
            }
            if(!dq && string_last(buffer) == '\n') {
                reading = false;
                break;
            }
        }
    }

    if(reading) {
        string_clear(buffer);
        goto read_more;
    }

    return SUCCESS;
}
