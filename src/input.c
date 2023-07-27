#include "ashe_utils.h"
#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

/// Flag that gets set if the rcmdline routine
/// gets interrupted while it is still reading
volatile atomic_bool reading = false;
/// Shell terminal modes
struct termios dflterm, rawterm;

static void inbuff_update_cursor(inbuff_t *buffer);
static void inbuff_cursor_right(inbuff_t *buffer, size_t max_col);
static void inbuff_cursor_left(inbuff_t *buffer, size_t max_col);
static void inbuff_remove(inbuff_t *inbuff, size_t maxcol);
static void inbuff_insert(inbuff_t *inbuff, char c, size_t maxcol);
static int get_terminal_width(void);

void pprompt(void)
{
    byte username[MAXNAME];
    struct utsname system;

    if(__glibc_unlikely(uname(&system) < 0)) {
        PW_USERNAME;
        die();
    }

    if(__glibc_unlikely(getlogin_r(username, MAXNAME) < 0)) {
        PW_SYSNAME;
        die();
    }

    byte jobs[40];
    size_t jobn = joblist_len();

    if(jobn > 0)
        sprintf(jobs, obrack bold(byellow("%ld")) cbrack, jobn);
    else
        jobs[0] = NULL_TERM;

    fprintf(
        stderr,
        "\r\n"
        "%s" obrack bold(bred("ashe")) cbrack obrack bold(magenta("%s")) bold(cyan("@"))
            bold(bwhite("%s")) cbrack bwhite(": "),
        jobs,
        username,
        system.sysname);
    fflush(stderr);
}

void init_rawterm(void)
{
    tcgetattr(TERMINAL_FD, &rawterm);
    rawterm = dflterm;
    rawterm.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    rawterm.c_oflag &= ~(OPOST);
    rawterm.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    rawterm.c_cflag |= (CS8);
    rawterm.c_cc[VMIN] = 1;
    rawterm.c_cc[VTIME] = 0;
}

void init_dflterm(void)
{
    tcgetattr(TERMINAL_FD, &dflterm);
}

void set_rawtmode(void)
{
    tcsetattr(TERMINAL_FD, TCSAFLUSH, &rawterm);
}

void set_dflmode(void)
{
    tcsetattr(TERMINAL_FD, TCSAFLUSH, &dflterm);
}

static int get_terminal_width(void)
{
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    return ws.ws_col;
}

static void inbuff_insert(inbuff_t *inbuff, char c, size_t maxcol)
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
        hide_cur;
        if(inbuff->cur_col >= maxcol) {
            mv_cur_down;
            mv_cur_col(1);
        }
        sv_cur_pos;
        for(size_t i = inbuff->curp; i < inbuff->len; i++)
            putc(inbuff->buffer[i], stderr);
        ld_cur_pos;
        show_cur;
    }
}

static void inbuff_remove(inbuff_t *inbuff, size_t maxcol)
{
    if(inbuff->curp > 0) {
        byte *src = inbuff->buffer + inbuff->curp;
        size_t n = inbuff->len - inbuff->curp;
        memmove(src - 1, src, n);
        inbuff->len--;
        inbuff->curp--;
        hide_cur;
        if(inbuff->cur_col <= 1) {
            mv_cur_up;
            mv_cur_col(maxcol);
        } else
            del_char;
        sv_cur_pos;
        for(size_t i = inbuff->curp; i <= inbuff->len; i++)
            putc(inbuff->buffer[i], stderr);
        fflush(stderr);
        del_char;
        ld_cur_pos;
        show_cur;
    }
}

void inbuff_print(inbuff_t *buffer, bool interrupted)
{
    size_t len = buffer->len;
    hide_cur;
    for(size_t i = 0; i < len; i++)
        putc(buffer->buffer[i], stderr);
    if(interrupted)
        for(size_t i = buffer->len; i > buffer->curp; i--)
            mv_cur_left;
    show_cur;
    fflush(stderr);
}

static void inbuff_cursor_left(inbuff_t *buffer, size_t max_col)
{
    if(buffer->curp > 0) {
        buffer->curp--;
        if(buffer->cur_col > 1) {
            mv_cur_left;
        } else {
            hide_cur;
            mv_cur_up;
            mv_cur_col(max_col);
            show_cur;
        }
    }
}

static void inbuff_cursor_right(inbuff_t *buffer, size_t max_col)
{
    if(buffer->curp < buffer->len) {
        buffer->curp++;
        if(buffer->cur_col < max_col) {
            mv_cur_right;
        } else {
            hide_cur;
            mv_cur_down;
            mv_cur_col(1);
            show_cur;
        }
    }
}

static void inbuff_update_cursor(inbuff_t *buffer)
{
    byte buf[32];
    byte c;
    size_t i = 0;
    ssize_t row, col;

    req_cur_pos;

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

    fflush(stderr);
    set_rawtmode();

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

            if(iscntrl(c)) {
                switch(c) {
                    case 13: // Enter or Ctrl-M
                        if(!dq && !escape) {
                            fprintf(stderr, "\r\n");
                            fflush(stderr);
                            goto end;
                        }
                        escape = false;
                        break;
                    case 15:
                    case 22: // Ctrl-V
                        // TODO: Implement clipboard paste
                        continue;
                    case 27: // Esc/Escape sequence
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
                            PW_TERMINR;
                            return FAILURE;
                        }
                        escape = false;
                        continue;
                    case 127: // Backspace (Delete)
                        if(buffer->len > 0)
                            inbuff_remove(buffer, max_col);
                        escape = false;
                        continue;
                    default:
                        continue;
                }
            } else {
                switch(c) {
                    case '"':
                        if(!escape)
                            dq ^= true;
                        escape = false;
                        break;
                    case '\\':
                        escape ^= true;
                        break;
                    default:
                        escape = false;
                        break;
                }
            }

            inbuff_insert(buffer, c, max_col);
            /// MAKE SURE we got interrupted
        } else if(n == -1 && errno == EINTR && (sigint_recv || sigchld_recv)) {
            sigint_recv = sigchld_recv = false;
            continue;
        } else {
            PW_TERMINR;
            return FAILURE;
        }
    }

end:
    assert(buffer->len <= MAXLINE - 1);
    buffer->buffer[buffer->len++] = '\0';
    reading = false;
    /// Enable interrupts and restore
    /// terminal default modes
    fflush(stderr);
    set_dflmode();
    unblock_sigint();
    unblock_sigchld();
    return SUCCESS;
}
