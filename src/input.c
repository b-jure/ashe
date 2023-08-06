#include "ashe_string.h"
#include "ashe_utils.h"
#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"
#include "shell.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

/*------------------------- PROMPT ------------------------*/

#define ASH_MAX_PLEN (ARG_MAX >> 2)

size_t _prompt_len = 0;
#define PLEN _prompt_len

#define ASH_P_JOBS   bold(byellow("%ld"))
#define ASH_P_PREFIX bold(bred("%s"))
#define ASH_P_USER   bold(magenta("%s"))
#define ASH_P_SEP    bold(cyan("%s"))
#define ASH_P_SYSTEM bold(bwhite("%s"))
#define ASH_P_SUFFIX bwhite("%s")

#define ASH_P_DEFAULT \
    bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM) ASH_P_SUFFIX
#define ASH_P_WJOBCOUNT                                                                        \
    bracketed(ASH_P_JOBS) bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM) \
        ASH_P_SUFFIX

/*---------------------------------------------------------*/

#define CTRL_KEY(k) ((k) &0x1f)
#define ESCAPE      27
#define CR          0x0D

#define CDIR_DOWN  1
#define CDIR_UP    2
#define CDIR_LEFT  4
#define CDIR_RIGHT 8
#define CDIR_ABSC  16
#define CDIR_ABS   32

#define redraw_terminal_cursor(CDIR, row, col)                                                     \
    do {                                                                                           \
        switch(CDIR) {                                                                             \
            case CDIR_DOWN: write_or_die(mv_cur_down(row), sizeof(mv_cur_down(row))); break;       \
            case CDIR_UP: write_or_die(mv_cur_up(row), sizeof(mv_cur_up(row))); break;             \
            case CDIR_LEFT: write_or_die(mv_cur_left(col), sizeof(mv_cur_left(col))); break;       \
            case CDIR_RIGHT: write_or_die(mv_cur_right(col), sizeof(mv_cur_right(col))); break;    \
            case(CDIR_UP | CDIR_LEFT):                                                             \
                write_or_die(                                                                      \
                    mv_cur_up(row) mv_cur_left(col), sizeof(mv_cur_up(row) mv_cur_left(col)));     \
                break;                                                                             \
            case(CDIR_UP | CDIR_RIGHT):                                                            \
                write_or_die(                                                                      \
                    mv_cur_up(row) mv_cur_right(col), sizeof(mv_cur_up(row) mv_cur_right(col)));   \
                break;                                                                             \
            case(CDIR_DOWN | CDIR_LEFT):                                                           \
                write_or_die(                                                                      \
                    mv_cur_down(row) mv_cur_left(col), sizeof(mv_cur_down(row) mv_cur_left(col))); \
                break;                                                                             \
            case(CDIR_DOWN | CDIR_RIGHT):                                                          \
                write_or_die(                                                                      \
                    mv_cur_down(row) mv_cur_right(col),                                            \
                    sizeof(mv_cur_down(row) mv_cur_right(col)));                                   \
                break;                                                                             \
            case(CDIR_DOWN | CDIR_ABSC):                                                           \
                write_or_die(                                                                      \
                    mv_cur_down(row) mv_cur_col(col), sizeof(mv_cur_down(row) mv_cur_col(col)));   \
                break;                                                                             \
            case(CDIR_UP | CDIR_ABSC):                                                             \
                write_or_die(                                                                      \
                    mv_cur_up(row) mv_cur_col(col), sizeof(mv_cur_up(row) mv_cur_col(col)));       \
                break;                                                                             \
            case(CDIR_LEFT | CDIR_ABS):                                                            \
                write_or_die(mv_cur_pos(row, col), sizeof(mv_cur_pos(row, col)));                  \
                break;                                                                             \
            default: break;                                                                        \
        }                                                                                          \
    } while(0)

typedef enum {
    BACKSPACE = 127,
    L_ARW = 1000,
    U_ARW,
    D_ARW,
    R_ARW,
    HOME_KEY,
    END_KEY,
    DEL_KEY
} terminal_key;

typedef struct {
    byte *start;
    size_t len;
} row_t;

#define IMPLEMENTED(c) (c != ESCAPE)

typedef uint16_t termkey_t;

static size_t inbuff_lenrel(inbuff_t *buffer);
static bool is_escaped(byte *bt, size_t curpos);
static bool in_dq(byte *str, size_t len);
static void inbuff_cursor_right(inbuff_t *buffer);
static void inbuff_cursor_left(inbuff_t *buffer);
static void inbuff_cursor_up(inbuff_t *buffer);
static void inbuff_cursor_down(inbuff_t *buffer);
static void inbuff_remove(inbuff_t *inbuff);
static void inbuff_insert(inbuff_t *inbuff, char c);
static bool inbuff_process_key(inbuff_t *buffer);
static int get_cursor_pos(uint16_t *row, uint16_t *col);
static termkey_t read_key();
static size_t prompt_len(const byte *prompt);

static size_t prompt_len(const byte *prompt)
{
    size_t len = 0;
    bool escape_seq = false;

    for(size_t i = 0; prompt[i]; i++) {
        if(escape_seq) {
            if(prompt[i] == 'm')
                escape_seq = false;
        } else if(prompt[i] == '\033') {
            escape_seq = true;
        } else {
            len++;
        }
    }

    return len;
}

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

    byte prompt[ASH_MAX_PLEN] = {0};
    size_t jobn = joblist_len(&shell.sh_jlist);

    if(jobn <= 0) {
        sprintf(prompt, ASH_P_DEFAULT, "ashe", username, "@", system.sysname, ": ");
    } else {
        sprintf(prompt, ASH_P_WJOBCOUNT, jobn, "ashe", username, "@", system.sysname, ": ");
    }

    _prompt_len = prompt_len(prompt);

    fprintf(stderr, "\r\n%s", prompt);
    fflush(stderr);
}

void terminal_init(terminal_t *term)
{
    get_size_or_die(&term->rows, &term->columns);
    memset(&term->tm_inbuff, 0, sizeof(term->tm_inbuff));

    if(is_null(term->tm_inbuff.in_rows = vec_with_capacity(sizeof(row_t), 1))) {
        exit(EXIT_FAILURE);
    }

    term->tm_reading = 0;
    init_rawterm(&term->tm_rawterm);
    init_dflterm(&term->tm_dflterm);
}

void init_rawterm(struct termios *rawterm)
{
    tcgetattr(TERMINAL_FD, rawterm);
    rawterm->c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    rawterm->c_oflag &= ~(OPOST);
    rawterm->c_lflag &= ~(ECHO | ICANON | IEXTEN);
    rawterm->c_cflag |= (CS8);
    rawterm->c_cc[VMIN] = 1;
    rawterm->c_cc[VTIME] = 0;
}

void init_dflterm(struct termios *dflterm)
{
    tcgetattr(TERMINAL_FD, dflterm);
}

void settmode(struct termios *tmode)
{
    tcsetattr(TERMINAL_FD, TCSAFLUSH, tmode);
}

int get_window_size(uint16_t *height, uint16_t *width)
{
    struct winsize ws;
    if(__glibc_unlikely(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0)) {
        return get_window_size_fallback(height, width);
    } else {
        if(is_some(height))
            *height = ws.ws_row;
        if(is_some(width))
            *width = ws.ws_col;

        return SUCCESS;
    }
}

int get_window_size_fallback(uint16_t *height, uint16_t *width)
{
    size_t n = sizeof(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos);
    write_or_die(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos, n);
    return get_cursor_pos(height, width);
}

static bool in_dq(byte *str, size_t len)
{
    bool dq = false;
    while(len--)
        if(*str++ == '"')
            dq ^= true;
    return dq;
}

static bool is_escaped(byte *bt, size_t curpos)
{
    byte *at = bt + curpos;
    return (
        (curpos > 1 && *(at - 1) == '\\' && *(at - 2) != '\\')
        || (curpos == 1 && *(at - 1) == '\\'));
}

static int get_cursor_pos(uint16_t *row, uint16_t *col)
{
    byte c;
    size_t i = 0;
    int nread;
    uint16_t srow, scol;
    byte buf[32];

    write_or_die(req_cur_pos, sizeof(req_cur_pos));

    while(i < sizeof(buf) && (nread = read(STDIN_FILENO, &c, 1)) == 1) {
        if(c == 'R')
            break;
        buf[i++] = c;
    }

    if(nread == -1) {
        perr();
        return FAILURE;
    }

    sscanf(buf, "\033[%hu;%hu", &srow, &scol);

    if(is_some(row))
        *row = srow;
    if(is_some(col))
        *col = scol;

    return SUCCESS;
}

static void shift_rows_right(inbuff_t *inbuff, size_t start)
{
    vec_t *rows = inbuff->in_rows;
    size_t len = vec_len(rows);

    for(size_t i = start; i < len; i++) {
        fflush(stdout);
        ((row_t *) vec_index(rows, i))->start++;
    }
}

__attribute__((unused)) static void shift_rows_left(inbuff_t *inbuff, size_t start)
{
    vec_t *rows = inbuff->in_rows;
    size_t len = vec_len(rows);

    for(size_t i = start; i < len; i++) {
        ((row_t *) vec_index(rows, i))->start--;
    }
}
static size_t inbuff_lenrel(inbuff_t *buffer)
{
    size_t len = 0;
    vec_t *rows = buffer->in_rows;
    row_t *row;

    for(size_t i = 0; i < buffer->in_bcur.cr_row; i++) {
        row = vec_index(rows, i);
        len += row->len;
    }

    len += buffer->in_bcur.cr_col;

    return len;
}

static bool inbuff_cr(inbuff_t *buffer)
{
    uint16_t row_n = buffer->in_bcur.cr_row;
    vec_t *rows = buffer->in_rows;
    row_t *backrow = vec_index(rows, row_n);

    if(is_escaped(backrow->start, buffer->in_bcur.cr_col)
       || in_dq(buffer->in_buffer, inbuff_lenrel(buffer)))
    {
        inbuff_insert(buffer, '\n');

        uint16_t column_n = buffer->in_bcur.cr_col;
        row_t row = {
            .start = backrow->start + column_n,
            .len = backrow->len - column_n,
        };

        backrow->len = column_n;
        vec_insert(rows, &row, row_n);
        buffer->in_bcur.cr_row++;
        buffer->in_bcur.cr_col = 0;

        return false;
    } else {
        return true;
    }
}

static row_t *inbuff_row_current(inbuff_t *buffer)
{
    return vec_index(buffer->in_rows, buffer->in_bcur.cr_row);
}

static void inbuff_insert(inbuff_t *buffer, char c)
{
    if(buffer->in_len < MAXLINE - 1) {
        uint16_t maxcol = shell.sh_term.columns;
        row_t *row = inbuff_row_current(buffer);
        size_t lenrel = inbuff_lenrel(buffer);
        size_t n = buffer->in_len - lenrel;
        byte *src = row->start + buffer->in_bcur.cr_col;

        memmove(src + 1, src, n);
        *src = c;
        shift_rows_right(buffer, buffer->in_bcur.cr_row + 1);
        row->len++;

        size_t cap = n + sizeof("\r\n") + (2 * sizeof(byte));
        byte buff[cap];
        buff[0] = '\0';

        strncat(buff, &c, sizeof(byte));

        if(__glibc_unlikely(buffer->in_tcur.cr_col >= maxcol)) {
            buffer->in_tcur.cr_row++;
            buffer->in_tcur.cr_col = 1;
            strcat(buff, "\r\n");
        } else {
            buffer->in_tcur.cr_col++;
        }

        buffer->in_bcur.cr_col++;
        buffer->in_len++;

        strcat(buff, sv_cur_pos);
        strncat(buff, row->start + buffer->in_bcur.cr_col, n);
        strcat(buff, ld_cur_pos);

        if(c == '\n')
            strcat(buff, "\r");

        write_or_die(buff, strlen(buff));
    }
}

static void inbuff_remove(inbuff_t *inbuff)
{
    if(inbuff->in_len > 0) {
        // TODO: Implement
    }
}

void inbuff_redraw(inbuff_t *buffer)
{
    uint16_t maxcol = shell.sh_term.columns;
    size_t len = buffer->in_len;
    size_t cursor_pos_from_back = len - inbuff_lenrel(buffer);

    printf("%*s", (int32_t) len, buffer->in_buffer);
    get_cursor_pos(&buffer->in_tcur.cr_row, &buffer->in_tcur.cr_col);

    vec_t *rows = buffer->in_rows;
    size_t ridx = vec_len(rows) - 1;
    row_t *row = vec_index(rows, ridx--);
    size_t rown = row->len;

    size_t up = 0;
    while(cursor_pos_from_back--) {
        if(rown-- <= 0) {
            row = vec_index(rows, ridx--);
            rown = row->len;
            buffer->in_tcur.cr_col = rown % maxcol;
        }

        if(buffer->in_tcur.cr_col == 1) {
            up++;
            buffer->in_tcur.cr_col = maxcol;
        } else
            buffer->in_tcur.cr_col--;
    }

    byte buff[sizeof(CSI "A+") + UINT_DIGITS];
    buff[0] = '\0';
    write_or_die(buff, sprintf(buff, CSI "%luA" CSI "%uG", up, buffer->in_tcur.cr_col));
}

void inbuff_clear(inbuff_t *buffer)
{
    vec_clear_capacity(buffer->in_rows, NULL);

    if(!vec_push(
           buffer->in_rows,
           &(row_t){
               .len = 0,
               .start = buffer->in_buffer,
           }))
    {
        exit(EXIT_FAILURE);
    }

    buffer->in_len = 0;
    buffer->in_bcur.cr_col = 0;
    buffer->in_bcur.cr_row = 0;
}

static void inbuff_cursor_left(inbuff_t *buffer)
{
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    uint16_t maxcol = shell.sh_term.columns;
    byte buff[sizeof(CSI CSI "ADG+") + (2 * UINT_DIGITS)];
    buff[0] = '\0';

    if(buf_cursor->cr_col > 0) {
        --buf_cursor->cr_col;

        if(term_cursor->cr_col == 1) {
            term_cursor->cr_col = maxcol;
            --term_cursor->cr_row;
            write_or_die(buff, sprintf(buff, CSI "%uA" CSI "%uG", 1, maxcol));
        } else {
            --term_cursor->cr_col;
            write_or_die(mv_cur_left(1), sizeof(mv_cur_left(1)));
        }
    } else if(buf_cursor->cr_row > 0) {
        row_t *row = vec_index(buffer->in_rows, --buf_cursor->cr_row);
        buf_cursor->cr_col = row->len - 1;

        --term_cursor->cr_row;
        term_cursor->cr_col = row->len % maxcol;

        /* In case this is the row with prompt add prompt len */
        if(buf_cursor->cr_row == 0)
            term_cursor->cr_col += PLEN;

        write_or_die(buff, sprintf(buff, CSI "%uA" CSI "%uG", 1, term_cursor->cr_col));
    }
} // OK

static void inbuff_cursor_right(inbuff_t *buffer)
{
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    row_t *row = vec_index(buffer->in_rows, buf_cursor->cr_row);
    size_t nrow = vec_len(buffer->in_rows);

    uint16_t maxcol = shell.sh_term.columns;

    if(buf_cursor->cr_col < row->len) {
        ++buf_cursor->cr_col;

        if(term_cursor->cr_col < maxcol) {
            ++term_cursor->cr_col;
            redraw_terminal_cursor(CDIR_RIGHT, 0, 1);
        } else {
            term_cursor->cr_col = 1;
            ++term_cursor->cr_row;
            redraw_terminal_cursor(CDIR_DOWN | CDIR_ABSC, 1, 1);
        }
    } else if(buf_cursor->cr_row < (nrow - 1)) {
        row = vec_index(buffer->in_rows, ++buf_cursor->cr_row);
        buf_cursor->cr_col = 0;
        ++term_cursor->cr_row;
        term_cursor->cr_col = 1;
        redraw_terminal_cursor(CDIR_DOWN | CDIR_ABSC, 1, 1);
    }
} // OK

static void inbuff_cursor_up(inbuff_t *buffer)
{
    uint16_t maxcol = shell.sh_term.columns;
    uint16_t last_terminal_column_up;
    vec_t *rows = buffer->in_rows;
    row_t *row;
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    bool redraw = false;

    if(buf_cursor->cr_row > 0) {
        --term_cursor->cr_row;
        if(buf_cursor->cr_col >= maxcol) {
            buf_cursor->cr_col -= maxcol;
        } else {
            row = vec_index(rows, --buf_cursor->cr_row);
            bool last_row = buf_cursor->cr_row == 0;

            if(row->len + ((last_row) ? PLEN : 0) > maxcol) {
                last_terminal_column_up = ((row->len + ((last_row) ? PLEN : 0) - 1) % maxcol) + 1;

                if(last_terminal_column_up - 1 >= buf_cursor->cr_col) {
                    buf_cursor->cr_col += (row->len - last_terminal_column_up);
                } else {
                    buf_cursor->cr_col = row->len - 1;
                    term_cursor->cr_col = last_terminal_column_up;
                }
            } else {
                if(last_row) {
                    if(buf_cursor->cr_col <= PLEN) {
                        buf_cursor->cr_col = 0;
                        term_cursor->cr_col = PLEN + 1;
                    } else {
                        buf_cursor->cr_col -= PLEN;
                    }
                } else if(buf_cursor->cr_col >= row->len - 1) {
                    buf_cursor->cr_col = row->len - 1;
                    term_cursor->cr_col = row->len;
                }
            }
        }
        redraw = true;
    } else if(buf_cursor->cr_col + PLEN >= maxcol) {
        --term_cursor->cr_row;
        if(buf_cursor->cr_col + PLEN - maxcol < maxcol) {
            if(buf_cursor->cr_col % maxcol <= PLEN) {
                buf_cursor->cr_col = 0;
                term_cursor->cr_col = PLEN + 1;
            } else {
                buf_cursor->cr_col -= maxcol;
            }
        } else {
            buf_cursor->cr_col -= maxcol;
        }
        redraw = true;
    }

    if(redraw) {
        byte buff[sizeof(CSI CSI "AG++") + (2 * UINT_DIGITS)];
        buff[0] = '\0';
        write_or_die(buff, sprintf(buff, CSI "%uA" CSI "%uG", 1, term_cursor->cr_col));
    }
} // OK

static void inbuff_cursor_down(inbuff_t *buffer)
{
    uint16_t maxcol = shell.sh_term.columns;
    vec_t *rows = buffer->in_rows;
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    uint16_t last_row = vec_len(rows) - 1;
    row_t *row = vec_index(rows, buf_cursor->cr_row);
    bool redraw = false;

    if(buf_cursor->cr_row > 0) {
        /// Case: row is not a prompt row
        if((buf_cursor->cr_col / maxcol) < ((row->len - 1) / maxcol)) {
            /// Case: row with multiple terminal rows and
            /// column is not in the last terminal row
            if(row->len - 1 - maxcol >= buf_cursor->cr_col) {
                buf_cursor->cr_col += maxcol;
            } else {
                buf_cursor->cr_col = row->len - 1;
                term_cursor->cr_col = row->len % maxcol;
            }
            redraw = true;
        } else if(buf_cursor->cr_row < last_row) {
            /// Case: row with a single terminal row
            row = vec_index(rows, ++buf_cursor->cr_row);
            if(row->len - 1 < buf_cursor->cr_col) {
                buf_cursor->cr_col = row->len - 1;
                term_cursor->cr_col = row->len % maxcol;
            }
            redraw = true;
        }
    } else {
        /// Case: row is first
        if((buf_cursor->cr_col + PLEN / maxcol) < ((row->len + PLEN - 1) / maxcol)) {
            /// Case: row with multiple terminal rows and
            /// column is not in the last terminal row
            if(row->len + PLEN - 1 - maxcol >= buf_cursor->cr_col + PLEN) {
                buf_cursor->cr_col = PLEN + maxcol;
            } else {
                buf_cursor->cr_col = row->len + PLEN - 1;
                term_cursor->cr_col = (row->len + PLEN) % maxcol;
            }
            redraw = true;
        } else if(0 < last_row) {
            /// Case: row with a single terminal row
            row = vec_index(rows, ++buf_cursor->cr_row);
            if(row->len - 1 < buf_cursor->cr_col + PLEN) {
                buf_cursor->cr_col = row->len - 1;
                term_cursor->cr_col = row->len % maxcol;
            } else {
                buf_cursor->cr_col = buf_cursor->cr_col + PLEN;
            }
            redraw = true;
        }
    }

    if(redraw) {
        byte buff[sizeof(CSI CSI "BG++") + (2 * UINT_DIGITS)];
        buff[0] = '\0';
        write_or_die(buff, sprintf(buff, CSI "%hdB" CSI "%hdG", 1, term_cursor->cr_col));
    }
}

void goto_eol(inbuff_t *buffer)
{
    uint16_t maxcol = shell.sh_term.columns;
    uint16_t current_row = buffer->in_bcur.cr_row;
    uint16_t current_column = buffer->in_bcur.cr_col + ((current_row == 0) ? PLEN : 0);
    row_t *row = vec_index(buffer->in_rows, current_row);
    size_t row_len = row->len + ((current_row == 0) ? PLEN : 0);

    if(current_column != maxcol - 1 || current_column != row_len - 1) {
        uint16_t column;
        unused(column); /* Compiler doesn't recognize it is used in macro below */

        if(row_len > maxcol) {
            if((current_column / maxcol) == ((row_len - 1) / maxcol)) {
                column = row_len % maxcol;
            } else {
                column = maxcol;
            }
        } else {
            column = row_len;
        }

        byte out[sizeof(mv_cur_col() "+") + UINT_DIGITS];
        out[0] = '\0';
        write_or_die(out, sprintf(out, CSI "%uG", column));
    }
}

void goto_home(inbuff_t *buffer)
{
    uint16_t maxcol = shell.sh_term.columns;
    uint16_t current_row = buffer->in_bcur.cr_row;
    uint16_t current_column = buffer->in_bcur.cr_col + ((current_row == 0) ? PLEN : 0);

    if(current_column % maxcol != 0) {
        current_column = current_column - (current_column % maxcol);
        buffer->in_bcur.cr_col = current_column;

        byte out[sizeof(CSI "G+") + UINT_DIGITS];
        out[0] = '\0';
        write_or_die(out, sprintf(out, CSI "%uG", current_column + 1));
    }
}

static void clear_screen(inbuff_t *buffer)
{
    write_or_die(clrscr mv_cur_home, sizeof(clrscr mv_cur_home));
    pprompt();
    inbuff_redraw(buffer);
}

static termkey_t read_key(void)
{
    int nread;
    byte c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && (errno != EINTR && !shell.sh_intr)) {
            ATOMIC_PRINT({
                PW_TERMINR;
                die();
            });
        }
    }

    mask_signal(SIGCHLD, SIG_BLOCK);
    mask_signal(SIGINT, SIG_BLOCK);
    mask_signal(SIGWINCH, SIG_BLOCK);

    if(c == ESCAPE) {
        byte seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1)
            return ESCAPE;
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
            return ESCAPE;

        if(seq[0] == '[') {
            if(isdigit(seq[1])) {
                if(read(STDIN_FILENO, &seq[2], 1) != 1)
                    return ESCAPE;
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '3': return DEL_KEY;
                        case '1':
                        case '7': return HOME_KEY;
                        case '4':
                        case '8': return END_KEY;
                        default: break;
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'C': return R_ARW;
                    case 'D': return L_ARW;
                    case 'B': return D_ARW;
                    case 'A': return U_ARW;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                    default: break;
                }
            }
        } else if(seq[0] == 'O') {
            switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return HOME_KEY;
                default: break;
            }
        }

        return ESCAPE;
    } else {
        return c;
    }
}

static bool inbuff_process_key(inbuff_t *buffer)
{
    termkey_t c;

    c = read_key();

    if((IMPLEMENTED(c))) {
        switch(c) {
            case CR:
                if(inbuff_cr(buffer))
                    return false;
                break;
            case DEL_KEY:
            case BACKSPACE: inbuff_remove(buffer); break;
            case END_KEY: goto_eol(buffer); break;
            case HOME_KEY: goto_home(buffer); break;
            case CTRL_KEY('l'): clear_screen(buffer); break;
            case L_ARW: inbuff_cursor_left(buffer); break;
            case R_ARW: inbuff_cursor_right(buffer); break;
            case U_ARW: inbuff_cursor_up(buffer); break;
            case D_ARW: inbuff_cursor_down(buffer); break;
            /// Unimplemented stuff
            case CTRL_KEY('x'):
            case CTRL_KEY('h'):
            case CTRL_KEY('j'):
            case CTRL_KEY('k'):
            case CTRL_KEY('i'): break;
            default: inbuff_insert(buffer, c); break;
        }
    }

    return true;
}

void read_input(inbuff_t *buffer)
{
    /* Set reading flag in case we get interrupted */
    shell.sh_term.tm_reading = true;
    fflush(stderr);
    /* Set raw mode */
    settmode(&shell.sh_term.tm_rawterm);
    /* Get terminal size */
    get_size_or_die(&shell.sh_term.rows, &shell.sh_term.columns);
    /* Get terminal cursor position */
    cursor_t *terminal_cursor = &shell.sh_term.tm_inbuff.in_tcur;
    get_cursor_pos(&terminal_cursor->cr_row, &terminal_cursor->cr_col);
    /* Loop until newline */
    while(inbuff_process_key(buffer)) {
        mask_signal(SIGCHLD, SIG_UNBLOCK);
        mask_signal(SIGINT, SIG_UNBLOCK);
        mask_signal(SIGWINCH, SIG_UNBLOCK);
    }
    /* Null-Terminate the buffer */
    buffer->in_buffer[buffer->in_len++] = '\0';
    /* Unset the reading flag */
    shell.sh_term.tm_reading = false;
    fflush(stderr);
    /* Set default shell terminal mode */
    settmode(&shell.sh_term.tm_dflterm);
}
