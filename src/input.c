#include "ashe_utils.h"
#include "async.h"
#include "input.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>

/// Flag that gets set if the rcmdline routine
/// gets interrupted while it is still reading
volatile atomic_bool reading = false;
/// Shell terminal modes
struct termios dflterm, rawterm;

void inbuff_update_cursor(inbuff_t *buffer);

void init_rawterm(void)
{
    tcgetattr(TERMINAL_FD, &rawterm);
    rawterm = dflterm;
    rawterm.c_lflag &= ~(ECHO | ICANON);
    rawterm.c_cc[VMIN] = 1;
    rawterm.c_cc[VTIME] = 0;
}

void init_dflterm(void)
{
    tcgetattr(TERMINAL_FD, &dflterm);
}

void set_rawtmode(void)
{
    tcsetattr(TERMINAL_FD, TCSADRAIN, &rawterm);
}

void set_dflmode(void)
{
    tcsetattr(TERMINAL_FD, TCSADRAIN, &dflterm);
}

int get_terminal_width(void)
{
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    return ws.ws_col;
}

void inbuff_insert(inbuff_t *inbuff, char c, size_t maxcol)
{
    if(inbuff->len < MAXLINE - 1) {
        byte *src = inbuff->buffer + inbuff->curp;
        size_t n = inbuff->len - inbuff->curp;
        memmove(src + 1, src, n);
        memcpy(src, &c, sizeof(char));
        inbuff->len++;
        inbuff->curp++;
        putc(c, stderr);
        fflush(stderr);
        if(inbuff->cur_col >= maxcol) {
            mv_cur_down(stderr);
            mv_cur_col(stderr, (size_t) 1);
        }
        sv_cur_pos(stderr);
        for(size_t i = inbuff->curp; i < inbuff->len; i++) {
            putc(inbuff->buffer[i], stderr);
            fflush(stderr);
        }
        ld_cur_pos(stderr);
    }
}

void inbuff_remove(inbuff_t *inbuff)
{
    if(inbuff->curp > 0) {
        byte *src = inbuff->buffer + inbuff->curp;
        size_t n = inbuff->len - inbuff->curp;
        memmove(src - 1, src, n);
        inbuff->len--;
        inbuff->curp--;
        del_char(stderr);
        sv_cur_pos(stderr);
        for(size_t i = inbuff->curp; i <= inbuff->len; i++) {
            putc(inbuff->buffer[i], stderr);
            fflush(stderr);
        }
        del_char(stderr);
        ld_cur_pos(stderr);
    }
}

void inbuff_print(inbuff_t *buffer)
{
    size_t len = buffer->len;
    for(size_t i = 0; i < len; i++)
        putc(buffer->buffer[i], stderr);
}

void inbuff_cursor_left(inbuff_t *buffer, size_t max_col)
{
    if(buffer->curp > 0) {
        buffer->curp--;
        if(buffer->cur_col > 1) {
            mv_cur_left(stderr);
        } else {
            mv_cur_up(stderr);
            mv_cur_col(stderr, max_col);
        }
    }
}

void inbuff_cursor_right(inbuff_t *buffer, size_t max_col)
{
    if(buffer->curp < buffer->len) {
        buffer->curp++;
        if(buffer->cur_col < max_col) {
            mv_cur_right(stderr);
        } else {
            mv_cur_down(stderr);
            mv_cur_col(stderr, (size_t) 1);
        }
    }
}

void inbuff_update_cursor(inbuff_t *buffer)
{
    byte buf[32];
    byte c;
    size_t i = 0;
    ssize_t row, col;

    req_cur_pos(stderr);

    while(i < sizeof(buf) && read(STDIN_FILENO, &c, 1) == 1) {
        if(c == 'R')
            break;
        buf[i++] = c;
    }

    sscanf(buf, "\033[%ld;%ld", &row, &col);

    buffer->cur_col = col;
}

int read_input(inbuff_t *buffer)
{
    reading = true;

    char c;
    int n;
    size_t max_col;
    byte seq[2];
    bool escape = false;
    bool blocked = false;
    bool dq = false;

    set_rawtmode();
    fflush(stderr);

    while(1) {
        inbuff_update_cursor(buffer);

        if(blocked) {
            unblock_sigint();
            unblock_sigchld();
            blocked = false;
        }

        if((n = read(STDIN_FILENO, &c, 1)) == 1) {
            block_sigchld();
            block_sigint();
            blocked = true;

            max_col = get_terminal_width();

            fflush(stderr);

            switch(c) {
                case 127:
                    if(buffer->len > 0) {
                        inbuff_remove(buffer);
                        // TODO: reprint after remove
                    }
                    escape = false;
                    continue;
                case 27:
                    if((n = read(STDIN_FILENO, &seq, 2)) == 2) {
                        if(seq[0] == '[') {
                            switch(seq[1]) {
                                case 'D':
                                    inbuff_cursor_left(buffer, max_col);
                                    escape = false;
                                    continue;
                                case 'C':
                                    inbuff_cursor_right(buffer, max_col);
                                    escape = false;
                                    continue;
                                /// TODO: Implement input history
                                case 'A': // up
                                case 'B': // down
                                default:
                                    escape = false;
                                    continue;
                            }
                        }
                    } else if(n == -1) {
                        pwarn("failed reading terminal input");
                        perr();
                        return FAILURE;
                    }
                    escape = false;
                    continue;
                case '"':
                    if(!escape)
                        dq ^= true;
                    escape = false;
                    break;
                case '\\':
                    escape ^= true;
                    break;
                case '\n':
                    if(!dq && !escape) {
                        putc('\n', stderr);
                        fflush(stderr);
                        goto end;
                    }
                    escape = false;
                    break;
                default:
                    escape = false;
                    break;
            }

            inbuff_insert(buffer, c, max_col);
            // TODO: reprint after insert
        } else if(n == -1 && errno == EINTR && (sigint_recv || sigchld_recv)) {
            sigint_recv = sigchld_recv = false;
            continue;
        } else {
            pwarn("failed reading terminal input");
            perr();
            return FAILURE;
        }
    }

end:
    assert(buffer->len <= MAXLINE - 1);
    buffer->buffer[buffer->len++] = '\0';
    reading = false;
    /// Enable interrupts and restore
    /// terminal default modes
    set_dflmode();
    unblock_sigint();
    unblock_sigchld();
    return SUCCESS;
}
