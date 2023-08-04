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

static size_t inbuff_len_to_cur(inbuff_t *buffer);
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
    term->tm_reading = 0;
    init_rawterm(&term->tm_rawterm);
    init_dflterm(&term->tm_dflterm);
    memset(&term->tm_inbuff, 0, sizeof(term->tm_inbuff));
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
    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) {
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
    return (
        (curpos > 1 && *(bt - 1) == '\\' && *(bt - 2) != '\\')
        || (curpos == 1 && *(bt - 1) == '\\'));
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
        ((row_t *) vec_index(rows, i))->start++;
    }
}

static void shift_rows_left(inbuff_t *inbuff, size_t start)
{
    vec_t *rows = inbuff->in_rows;
    size_t len = vec_len(rows);

    for(size_t i = start; i < len; i++) {
        ((row_t *) vec_index(rows, i))->start--;
    }
}

static void inbuff_insert(inbuff_t *buffer, char c)
{
    if(buffer->in_len < MAXLINE - 1) {
        uint16_t maxcol;
        get_size_or_die(NULL, &maxcol);

        size_t len_to_column = inbuff_len_to_cur(buffer);
        size_t n = buffer->in_len - len_to_column;
        byte *src = buffer->in_buffer + len_to_column;
        memmove(src + 1, src, n);
        *src = c;
        shift_rows_right(buffer, buffer->in_bcur.cr_row + 1);

        ((row_t *) vec_index(buffer->in_rows, buffer->in_bcur.cr_row++))->len++;

        if(__glibc_unlikely(buffer->in_tcur.cr_col >= maxcol)) {
            buffer->in_tcur.cr_row++;
            buffer->in_tcur.cr_col = 1;
            if(c != '\n') {
                redraw_terminal_cursor(CDIR_DOWN | CDIR_ABSC, 1, 1);
                putchar(c);
            } else {
                write_or_die("\r\n", sizeof("\r\n"));
            }
        } else {
            buffer->in_tcur.cr_col++;
            if(c != '\n') {
                redraw_terminal_cursor(CDIR_RIGHT, 0, 1);
                putchar(c);
            } else {
                buffer->in_tcur.cr_row++;
                write_or_die("\r\n", sizeof("\r\n"));
            }
        }

        fflush(stdout);
    }
}

static void inbuff_remove(inbuff_t *inbuff)
{
    if(inbuff->in_len > 0) {
        // TODO: Implement
    }
}

void inbuff_print(inbuff_t *buffer, bool interrupted)
{
    // TODO: Implement
}

static void inbuff_cursor_left(inbuff_t *buffer)
{
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;

    uint16_t maxcol;
    get_size_or_die(NULL, &maxcol);

    if(buf_cursor->cr_col > 0) {
        row_t *row = vec_index(buffer->in_rows, buf_cursor->cr_row);
        --buf_cursor->cr_col;

        if(term_cursor->cr_col == 1) {
            term_cursor->cr_col = maxcol;
            --term_cursor->cr_row;
            redraw_terminal_cursor(CDIR_UP | CDIR_ABSC, 1, maxcol);
        } else {
            --term_cursor->cr_col;
            redraw_terminal_cursor(CDIR_LEFT, 0, 1);
        }
    } else if(buf_cursor->cr_row > 0) {
        row_t *row = vec_index(buffer->in_rows, --buf_cursor->cr_row);
        buf_cursor->cr_col = row->len - 1;

        --term_cursor->cr_row;
        term_cursor->cr_col = maxcol - (row->len % maxcol);

        /* In case this is the row with prompt add prompt len */
        if(buf_cursor->cr_row == 0)
            term_cursor->cr_col += PLEN;

        redraw_terminal_cursor(CDIR_UP | CDIR_ABSC, 1, term_cursor->cr_col);
    }
} // OK

static void inbuff_cursor_right(inbuff_t *buffer)
{
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    row_t *row = vec_index(buffer->in_rows, buf_cursor->cr_row);
    size_t nrow = vec_len(buffer->in_rows);

    uint16_t maxcol;
    get_size_or_die(NULL, &maxcol);

    if(buf_cursor->cr_row < (row->len - 1)) {
        ++buf_cursor->cr_col;

        if(term_cursor->cr_row < maxcol) {
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
    uint16_t maxcol;
    uint16_t last_terminal_column_up;
    vec_t *rows = buffer->in_rows;
    row_t *row;
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    bool redraw = false;

    get_size_or_die(NULL, &maxcol);

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

    if(redraw)
        redraw_terminal_cursor(CDIR_UP | CDIR_ABSC, 1, term_cursor->cr_col);
} // OK

static void inbuff_cursor_down(inbuff_t *buffer)
{
    uint16_t maxcol;
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

    if(redraw)
        redraw_terminal_cursor(CDIR_DOWN | CDIR_ABSC, 1, term_cursor->cr_col);
}

void goto_eol(inbuff_t *buffer)
{
}
//{
//    uint16_t maxcol;
//    get_window_size(NULL, &maxcol);
//
//    byte buff[maxcol * sizeof(mv_cur_right(1)) + sizeof(byte)];
//    buff[0] = '\0';
//
//    while(buffer->in_cpos < buffer->in_len && buffer->in_tcol < maxcol) {
//        buffer->in_cpos++;
//        buffer->in_tcol++;
//        strcat(buff, mv_cur_right(1));
//    }
//
//    write_or_die(buff, strlen(buff));
//}

void goto_home(inbuff_t *buffer)
{
    // uint16_t maxcol;
    // if(get_window_size(NULL, &maxcol) == FAILURE)
    //     die();

    // byte buff[maxcol * sizeof(mv_cur_left(1)) + sizeof(byte)];
    // buff[0] = '\0';

    // while(buffer->in_cpos > 0 && buffer->in_tcol > 1) {
    //     buffer->in_cpos--;
    //     buffer->in_tcol--;
    //     strcat(buff, mv_cur_left(1));
    // }

    // write_or_die(buff, strlen(buff));
}

static void clear_screen(inbuff_t *buffer)
{
    write_or_die(clrscr mv_cur_home, sizeof(clrscr mv_cur_home));
    pprompt();
    inbuff_print(buffer, true);
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

    block_sigchld();
    block_sigint();

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

static size_t inbuff_len_to_cur(inbuff_t *buffer)
{
    size_t len = 0;
    vec_t *rows = buffer->in_rows;

    for(size_t i = 0; i < buffer->in_bcur.cr_row; i++) {
        len += ((row_t *) vec_index(rows, i))->len;
    }

    len += buffer->in_bcur.cr_col;

    return len;
}

static byte *inbuff_current_row_col(inbuff_t *buffer)
{
    return ((row_t *) vec_index(buffer->in_rows, buffer->in_bcur.cr_row))->start
           + buffer->in_bcur.cr_col;
}

static void inbuff_cr(inbuff_t *buffer)
{
    vec_t *rows = buffer->in_rows;
    row_t *backrow = vec_index(rows, buffer->in_bcur.cr_row);
    uint16_t current_col = buffer->in_bcur.cr_col;

    if(is_escaped(buffer->in_buffer, buffer->in_len)
       || in_dq(buffer->in_buffer, inbuff_len_to_cur(buffer)))
    {
        inbuff_insert(buffer, '\n');

        // TODO: revise after implementing inbuff_insert

        row_t row = {
            .start = &buffer->in_buffer[current_col],
            .len = backrow->len - current_col,
        };

        backrow->len = current_col;

        vec_insert(rows, &row, buffer->in_bcur.cr_row);
    }
}

static bool inbuff_process_key(inbuff_t *buffer)
{
    termkey_t c;
    uint16_t max_col;
    cursor_t *buffer_cursor = &buffer->in_bcur;

    c = read_key();

    get_cursor_pos(&buffer_cursor->cr_row, &buffer_cursor->cr_col);

    if((IMPLEMENTED(c))) {
        switch(c) {
            case CR: return false; break;
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
    shell.sh_term.tm_reading = true;
    byte state = 1;

    fflush(stderr);
    settmode(&shell.sh_term.tm_rawterm);

    while(inbuff_process_key(buffer)) {
        unblock_sigint();
        unblock_sigchld();
    }

    buffer->in_buffer[buffer->in_len++] = '\0';
    shell.sh_term.tm_reading = false;
    fflush(stderr);
    settmode(&shell.sh_term.tm_dflterm);
}
