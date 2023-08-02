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

/*---------- FORMAT ------------*/

#define ASH_P_JOBS   bold(byellow("%ld"))
#define ASH_P_PREFIX bold(bred("%s"))
#define ASH_P_USER   bold(magenta("%s"))
#define ASH_P_SEP    bold(cyan("%s"))
#define ASH_P_SYSTEM bold(bwhite("%s"))
#define ASH_P_SUFFIX bwhite("%s")

#define ASH_P_DEFAULT                                                                              \
    bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM) ASH_P_SUFFIX
#define ASH_P_WJOBCOUNT                                                                            \
    bracketed(ASH_P_JOBS) bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM)     \
        ASH_P_SUFFIX

/*------------------------------*/

/*---------------------------------------------------------*/

/*
 *
 *
 *
 *
 */

#define CTRL_KEY(k) ((k) &0x1f)
#define ESCAPE      27
#define CR          0x0D

typedef enum {
    BACKSPACE = 127,
    L_ARW = 0x101,
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

#define IMPLEMENTED(c) (c != U_ARW && c != D_ARW && c != ESCAPE)

typedef uint16_t termkey_t;

static bool is_escaped(byte *bt, size_t curpos);
static bool in_dq(byte *str, size_t len);
static void inbuff_cursor_right(inbuff_t *buffer, byte *state);
static void inbuff_cursor_left(inbuff_t *buffer, byte *state);
static void inbuff_remove(inbuff_t *inbuff);
static void inbuff_insert(inbuff_t *inbuff, char c);
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

void settmode(struct termios *tmode)
{
    tcsetattr(TERMINAL_FD, TCSAFLUSH, tmode);
}

void init_dflterm(struct termios *dflterm)
{
    tcgetattr(TERMINAL_FD, dflterm);
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

int get_window_size_fallback(uint16_t *height, uint16_t *width)
{
    size_t n = sizeof(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos);
    write_or_die(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos, n);
    return get_cursor_pos(height, width);
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

static void inbuff_insert(inbuff_t *inbuff, char c)
{
    // if(inbuff->in_len < MAXLINE - 1) {
    //     uint16_t maxcol;
    //     get_window_size(NULL, &maxcol);
    //     byte *src = inbuff->in_buffer + inbuff->in_cpos;
    //     size_t n = inbuff->in_len - inbuff->in_cpos;
    //     size_t cap = sizeof(byte) + sizeof(sv_cur_pos ld_cur_pos) + sizeof("\r\n") + n;
    //     byte buff[cap + 10]; /// Add extra I am bad at math

    //    buff[0] = '\0';

    //    memmove(src + 1, src, n);
    //    memcpy(src, &c, sizeof(byte));

    //    inbuff->in_len++;
    //    inbuff->in_cpos++;

    //    strncat(buff, &c, sizeof(byte));

    //    if(inbuff->in_tcol >= maxcol)
    //        strcat(buff, "\r\n");

    //    strcat(buff, sv_cur_pos);
    //    strncat(buff, inbuff->in_buffer + inbuff->in_cpos, n);
    //    strcat(buff, ld_cur_pos);

    //    write_or_die(buff, strlen(buff));
    //}
}

static void inbuff_remove(inbuff_t *inbuff)
{
    // if(inbuff->in_cpos > 0) {

    //    byte *src = inbuff->in_buffer + inbuff->in_cpos;
    //    size_t n = inbuff->in_len - inbuff->in_cpos;
    //    size_t cap
    //        = sizeof(mv_cur_up(1) mv_cur_right(999) del_char sv_cur_pos clrscr_down
    //        ld_cur_pos) + n;
    //    byte buff[cap + 10];

    //    buff[0] = '\0';

    //    memmove(src - 1, src, n);

    //    inbuff->in_len--;
    //    inbuff->in_cpos--;

    //    if(inbuff->in_tcol <= 1)
    //        strcat(buff, mv_cur_up(1) mv_cur_col(999));
    //    else
    //        strcat(buff, del_char);

    //    strcat(buff, sv_cur_pos clrscr_down);
    //    strncat(buff, inbuff->in_buffer + inbuff->in_cpos, n);
    //    strcat(buff, ld_cur_pos);

    //    write_or_die(buff, strlen(buff));
    //}
}

void inbuff_print(inbuff_t *buffer, bool interrupted)
{
    // size_t len = buffer->in_len;
    // size_t n = buffer->in_len - buffer->in_cpos;
    // size_t cap = (sizeof(mv_cur_left(1)) * n) + len;
    // byte buff[cap + 10];

    // buff[0] = '\0';

    // strncat(buff, buffer->in_buffer, len);
    // if(interrupted)
    //     while(n--)
    //         strcat(buff, mv_cur_left(1));

    // write_or_die(buff, strlen(buff));
}

static void inbuff_cursor_left(inbuff_t *buffer, byte *state)
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
            write_or_die(mv_cur_up(1) mv_cur_col(maxcol), sizeof(mv_cur_up(1) mv_cur_col(maxcol)));
        } else {
            --term_cursor->cr_col;
            write_or_die(mv_cur_left(1), sizeof(mv_cur_left(1)));
        }
    } else if(buf_cursor->cr_row > 0) {
        row_t *row = vec_index(buffer->in_rows, --buf_cursor->cr_row);
        buf_cursor->cr_col = row->len - 1;

        --term_cursor->cr_row;
        term_cursor->cr_col = maxcol - (row->len % maxcol);

        /* In case this is the row with prompt add prompt len */
        if(buf_cursor->cr_row == 0)
            term_cursor->cr_col += PLEN;

        write_or_die(
            mv_cur_up(1) mv_cur_col(term_cursor->cr_col),
            sizeof(mv_cur_up(1) mv_cur_col(term_cursor->cur_col)));
    }
} // OK

static void inbuff_cursor_right(inbuff_t *buffer, byte *state)
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
            write_or_die(mv_cur_right(1), sizeof(mv_cur_right(1)));
        } else {
            term_cursor->cr_col = 1;
            ++term_cursor->cr_row;
            write_or_die(mv_cur_down(1) mv_cur_col(1), sizeof(mv_cur_down(1) mv_cur_col(1)));
        }
    } else if(buf_cursor->cr_row < (nrow - 1)) {
        row = vec_index(buffer->in_rows, ++buf_cursor->cr_row);
        buf_cursor->cr_col = 0;
        ++term_cursor->cr_row;
        term_cursor->cr_col = 1;
        write_or_die(mv_cur_down(1) mv_cur_col(1), sizeof(mv_cur_up(1) mv_cur_col(1)));
    }
} // OK

static void inbuff_cursor_up(inbuff_t *buffer, byte *state)
{
    uint16_t maxcol;
    uint16_t temp;
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    row_t *row = vec_index(buffer->in_rows, buf_cursor->cr_row);
    size_t nrow = vec_len(buffer->in_rows);

    get_size_or_die(NULL, &maxcol);

    if(buf_cursor->cr_col >= maxcol) {
        buf_cursor->cr_col -= maxcol;
        --term_cursor->cr_row;
        write_or_die(mv_cur_up(1), sizeof(mv_cur_up(1)));
    } else if(buf_cursor->cr_row > 0) {
        --term_cursor->cr_row;
        row = vec_index(buffer->in_rows, --buf_cursor->cr_row);
        temp = (row->len % maxcol) - 1;

        if(temp <= buf_cursor->cr_col) {
            buf_cursor->cr_col = row->len - 1;
            term_cursor->cr_col = temp + 1;
            write_or_die(
                mv_cur_up(1) mv_cur_col(term_cursor->cr_col),
                sizeof(mv_cur_up(1) mv_cur_col(term_cursor->cr_col)));
        } else if(row->len > maxcol) {
            buf_cursor->cr_col += maxcol;
            write_or_die(mv_cur_up(1), sizeof(mv_cur_up(1)));
        } else if(buf_cursor->cr_row == 0) {
            term_cursor->cr_col += PLEN;
            write_or_die(
                mv_cur_up(1) mv_cur_col(term_cursor->cr_col),
                sizeof(mv_cur_up(1) mv_cur_col(term_cursor->cr_col)));
        }
    }
} // OK

static void inbuff_cursor_down(inbuff_t *buffer, byte *state)
{
    uint16_t maxcol;
    cursor_t *buf_cursor = &buffer->in_bcur;
    cursor_t *term_cursor = &buffer->in_tcur;
    row_t *row = vec_index(buffer->in_rows, buf_cursor->cr_row);
    size_t nrow = vec_len(buffer->in_rows);

    if(buf_cursor->cr_row < row->len || buf_cursor->cr_row < nrow) {
        get_size_or_die(NULL, &maxcol);

        if(buf_cursor->cr_row < row->len) {
            ++buf_cursor->cr_col;

            if(term_cursor->cr_row < maxcol) {
                ++term_cursor->cr_col;
                write_or_die(mv_cur_right(1), sizeof(mv_cur_right(1)));
            } else {
                ++term_cursor->cr_row;
                term_cursor->cr_col = 1;
                write_or_die(mv_cur_down(1) mv_cur_col(1), sizeof(mv_cur_down(1) mv_cur_col(1)));
            }
        } else {
            row = vec_index(buffer->in_rows, ++buf_cursor->cr_row);
            buf_cursor->cr_col = 0;

            ++term_cursor->cr_row;
            term_cursor->cr_col = 1;

            write_or_die(mv_cur_down(1) mv_cur_col(1), sizeof(mv_cur_up(1) mv_cur_col(1)));
        }
    }
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

void handle_cr(

void inbuff_process_key(inbuff_t *buffer)
{
    termkey_t c;
    uint16_t max_col;
    cursor_t *buffer_cursor = &buffer->in_bcur;

    c = read_key();

    get_cursor_pos(&buffer_cursor->cr_row, &buffer_cursor->cr_col);

    if((IMPLEMENTED(c))) {
        switch(c) {
            case CR: break;
            case DEL_KEY:
            case BACKSPACE: inbuff_remove(buffer); break;
            case END_KEY: goto_eol(buffer); break;
            case HOME_KEY: goto_home(buffer); break;
            case CTRL_KEY('l'): clear_screen(buffer); break;
            case L_ARW: inbuff_cursor_left(buffer, state); break;
            case R_ARW: inbuff_cursor_right(buffer, state); break;
            /// Unimplemented stuff
            case CTRL_KEY('x'):
            case CTRL_KEY('h'):
            case CTRL_KEY('j'):
            case CTRL_KEY('k'):
            case CTRL_KEY('i'): break;
            default: inbuff_insert(buffer, c); break;
        }
    }

    if(c != '\\')
        CLRBIT(*state, S_ESC);
}

void read_input(inbuff_t *buffer)
{
    shell.sh_term.tm_reading = true;
    byte state = 1;

    fflush(stderr);
    settmode(&shell.sh_term.tm_rawterm);

    while(!TESTBIT(state, S_END)) {
        inbuff_process_key(buffer, &state);
        unblock_sigint();
        unblock_sigchld();
    }

    buffer->in_buffer[buffer->in_len++] = '\0';
    shell.sh_term.tm_reading = false;
    fflush(stderr);
    settmode(&shell.sh_term.tm_dflterm);
}
