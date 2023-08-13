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
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#undef inbuff /* Don't need it here */

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

#define ASH_P_DEFAULT bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM) ASH_P_SUFFIX
#define ASH_P_WJOBCOUNT \
    bracketed(ASH_P_JOBS) bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM) ASH_P_SUFFIX

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

// TODO: remove this piece of crap maybe?
#define redraw_terminal_cursor(CDIR, row, col)                                                              \
    do {                                                                                                    \
        switch(CDIR) {                                                                                      \
            case CDIR_DOWN: write_or_die(mv_cur_down(row), sizeof(mv_cur_down(row))); break;                \
            case CDIR_UP: write_or_die(mv_cur_up(row), sizeof(mv_cur_up(row))); break;                      \
            case CDIR_LEFT: write_or_die(mv_cur_left(col), sizeof(mv_cur_left(col))); break;                \
            case CDIR_RIGHT: write_or_die(mv_cur_right(col), sizeof(mv_cur_right(col))); break;             \
            case(CDIR_UP | CDIR_LEFT):                                                                      \
                write_or_die(mv_cur_up(row) mv_cur_left(col), sizeof(mv_cur_up(row) mv_cur_left(col)));     \
                break;                                                                                      \
            case(CDIR_UP | CDIR_RIGHT):                                                                     \
                write_or_die(mv_cur_up(row) mv_cur_right(col), sizeof(mv_cur_up(row) mv_cur_right(col)));   \
                break;                                                                                      \
            case(CDIR_DOWN | CDIR_LEFT):                                                                    \
                write_or_die(mv_cur_down(row) mv_cur_left(col), sizeof(mv_cur_down(row) mv_cur_left(col))); \
                break;                                                                                      \
            case(CDIR_DOWN | CDIR_RIGHT):                                                                   \
                write_or_die(                                                                               \
                    mv_cur_down(row) mv_cur_right(col), sizeof(mv_cur_down(row) mv_cur_right(col)));        \
                break;                                                                                      \
            case(CDIR_DOWN | CDIR_ABSC):                                                                    \
                write_or_die(mv_cur_down(row) mv_cur_col(col), sizeof(mv_cur_down(row) mv_cur_col(col)));   \
                break;                                                                                      \
            case(CDIR_UP | CDIR_ABSC):                                                                      \
                write_or_die(mv_cur_up(row) mv_cur_col(col), sizeof(mv_cur_up(row) mv_cur_col(col)));       \
                break;                                                                                      \
            case(CDIR_LEFT | CDIR_ABS):                                                                     \
                write_or_die(mv_cur_pos(row, col), sizeof(mv_cur_pos(row, col)));                           \
                break;                                                                                      \
            default: break;                                                                                 \
        }                                                                                                   \
    } while(0)

typedef enum {
    BACKSPACE = 127,
    L_ARW     = 1000,
    U_ARW,
    D_ARW,
    R_ARW,
    HOME_KEY,
    END_KEY,
    DEL_KEY
} terminal_key;

typedef struct {
    byte*  start;
    size_t len;
} row_t;

#define IMPLEMENTED(c)  (c != ESCAPE)
#define remainder(x, y) ((x) % (y))

typedef uint16_t termkey_t;

static size_t    inbuff_lenrel(inbuff_t* buffer);
static bool      inbuff_cursor_right(inbuff_t* buffer);
static bool      inbuff_cursor_left(inbuff_t* buffer);
static bool      inbuff_cursor_up(inbuff_t* buffer);
static bool      inbuff_cursor_down(inbuff_t* buffer);
static void      inbuff_remove(inbuff_t* inbuff);
static void      inbuff_insert(inbuff_t* inbuff, char c);
static bool      inbuff_process_key(inbuff_t* buffer);
static int       get_cursor_pos(uint16_t* row, uint16_t* col);
static termkey_t read_key();
static void      inbuff_debug_print(inbuff_t* buffer);

void pprompt(void)
{
    byte           username[MAXNAME];
    struct utsname system;

    if(__glibc_unlikely(uname(&system) < 0)) {
        PW_USERNAME;
        die();
    }

    if(__glibc_unlikely(getlogin_r(username, MAXNAME) < 0)) {
        PW_SYSNAME;
        die();
    }

    byte   prompt[ASH_MAX_PLEN] = {0};
    size_t jobn                 = joblist_len(&shell.sh_jlist);

    if(jobn <= 0) {
        sprintf(prompt, ASH_P_DEFAULT, "ashe", username, "@", system.sysname, ": ");
    } else {
        sprintf(prompt, ASH_P_WJOBCOUNT, jobn, "ashe", username, "@", system.sysname, ": ");
    }

    _prompt_len = len_without_seq(prompt);

    fprintf(stderr, "\r\n%s", prompt);
    fflush(stderr);
}

void terminal_init(terminal_t* term)
{
    get_size_or_die(&term->tm_rows, &term->tm_columns);
    memset(&term->tm_inbuff, 0, sizeof(term->tm_inbuff));

    if(is_null(term->tm_inbuff.in_rows = vec_with_capacity(sizeof(row_t), 1))) {
        exit(EXIT_FAILURE);
    }

    term->tm_reading = 0;
    init_rawterm(&term->tm_rawterm);
    init_dflterm(&term->tm_dflterm);
}

void init_rawterm(struct termios* rawterm)
{
    tcgetattr(TERMINAL_FD, rawterm);
    rawterm->c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    rawterm->c_oflag &= ~(OPOST);
    rawterm->c_lflag &= ~(ECHO | ICANON | IEXTEN);
    rawterm->c_cflag |= (CS8);
    rawterm->c_cc[VMIN]  = 1;
    rawterm->c_cc[VTIME] = 0;
}

void init_dflterm(struct termios* dflterm)
{
    tcgetattr(TERMINAL_FD, dflterm);
}

void settmode(struct termios* tmode)
{
    if(__glibc_unlikely(tcsetattr(TERMINAL_FD, TCSAFLUSH, tmode) < 0)) {
        die();
    }
}

int get_window_size(uint16_t* height, uint16_t* width)
{
    struct winsize ws;
    if(__glibc_unlikely(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0)) {
        return get_window_size_fallback(height, width);
    } else {
        if(is_some(height)) *height = ws.ws_row;
        if(is_some(width)) *width = ws.ws_col;

        return SUCCESS;
    }
}

int get_window_size_fallback(uint16_t* height, uint16_t* width)
{
    size_t n = sizeof(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos);
    write_or_die(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos, n);
    return get_cursor_pos(height, width);
}

static int get_cursor_pos(uint16_t* row, uint16_t* col)
{
    byte     c;
    size_t   i = 0;
    int      nread;
    uint16_t srow, scol;
    byte     buf[32];

    write_or_die(req_cur_pos, sizeof(req_cur_pos));

    while(i < sizeof(buf) && (nread = read(STDIN_FILENO, &c, 1)) == 1) {
        if(c == 'R') break;
        buf[i++] = c;
    }

    if(nread == -1) {
        perr();
        return FAILURE;
    }

    sscanf(buf, "\033[%hu;%hu", &srow, &scol);

    if(is_some(row)) *row = srow;
    if(is_some(col)) *col = scol;

    return SUCCESS;
}

static void shift_rows_right(inbuff_t* inbuff, size_t start)
{
    vec_t* rows = inbuff->in_rows;
    size_t len  = vec_len(rows);

    for(size_t i = start; i < len; i++) {
        ((row_t*) vec_index(rows, i))->start++;
    }
}

static void shift_rows_left(inbuff_t* inbuff, size_t start)
{
    vec_t* rows = inbuff->in_rows;
    size_t len  = vec_len(rows);

    for(size_t i = start; i < len; i++) {
        ((row_t*) vec_index(rows, i))->start--;
    }
}
static size_t inbuff_lenrel(inbuff_t* buffer)
{
    size_t len  = 0;
    vec_t* rows = buffer->in_rows;
    row_t* row;

    for(size_t i = 0; i < buffer->in_cur.cr_row; i++) {
        row = vec_index(rows, i);
        len += row->len;
    }

    len += buffer->in_cur.cr_col;

    return len;
}

static row_t* inbuff_row_current(inbuff_t* buffer)
{
    return vec_index(buffer->in_rows, buffer->in_cur.cr_row);
}

static bool inbuff_cr(inbuff_t* buffer)
{
    cursor_t* cursor      = &buffer->in_cur;
    uint16_t  old_row_idx = cursor->cr_row;
    vec_t*    rows        = buffer->in_rows;
    row_t*    old_row     = vec_index(rows, old_row_idx);

    if(is_escaped(old_row->start, cursor->cr_col) || in_dq(buffer->in_buffer, inbuff_lenrel(buffer))) {
        inbuff_insert(buffer, '\n');

        row_t new_row = {
            .start = old_row->start + cursor->cr_col,
            .len   = old_row->len - cursor->cr_col,
        };

        old_row->len = cursor->cr_col;

        terminal.tm_col = 1;
        cursor->cr_col  = 0;
        cursor->cr_row++;

        vec_insert(rows, &new_row, old_row_idx + 1);

        return false;
    } else {
        return true;
    }
}

static void inbuff_insert(inbuff_t* buffer, char c)
{
    if(buffer->in_len < MAXLINE - 1) {
        uint16_t  maxcol = terminal.tm_columns;
        cursor_t* cursor = &buffer->in_cur;
        row_t*    row    = inbuff_row_current(buffer);
        size_t    lenrel = inbuff_lenrel(buffer);
        size_t    n      = buffer->in_len - lenrel;
        byte*     src    = row->start + cursor->cr_col;

        memmove(src + 1, src, n);
        *src = c;
        shift_rows_right(buffer, cursor->cr_row + 1);
        row->len++;

        size_t cap = n + sizeof("\r\n" clrlneol clrscrd) + 10;
        byte   buff[cap];
        buff[0] = '\0';

        strcat(buff, clrlneol clrscrd);

        if(c != '\n') strncat(buff, &c, sizeof(byte));

        if(terminal.tm_col >= maxcol || c == '\n') {
            terminal.tm_col = 1;
            strcat(buff, "\r\n");
        } else {
            terminal.tm_col++;
        }

        cursor->cr_col++;
        buffer->in_len++;

        strcat(buff, sv_cur_pos);
        strncat(buff, row->start + cursor->cr_col, n);
        strcat(buff, ld_cur_pos);

        /* Turn on newline translation into "\r\n" */
        terminal.tm_rawterm.c_oflag |= (OPOST);
        settmode(&terminal.tm_rawterm);

        write_or_die(buff, strlen(buff));

        /* Turn off the newline translation */
        terminal.tm_rawterm.c_oflag &= ~(OPOST);
        settmode(&terminal.tm_rawterm);
    }
}

static void inbuff_remove(inbuff_t* buffer)
{
    cursor_t* cursor = &buffer->in_cur;

    if(cursor->cr_col > 0 || cursor->cr_row > 0) {
        vec_t* rows     = buffer->in_rows;
        row_t* row      = vec_index(rows, cursor->cr_row);
        bool   coalesce = false;

        size_t lenrel = inbuff_lenrel(buffer);
        size_t n      = buffer->in_len - lenrel;
        byte*  src    = row->start + cursor->cr_col;
        memmove(src - 1, src, n);
        shift_rows_left(buffer, cursor->cr_row + 1);
        row->len--;

        buffer->in_len--;

        /**
         * Before updating the row and column
         * check if the row needs to be coalesced.
         */
        if(cursor->cr_col == 0) {
            coalesce = true;
        }

        /* Move cursor, this updates the row and column. */
        inbuff_cursor_left(buffer);

        if(coalesce) {
            /* Coalesce the previous row */
            size_t len = row->len;
            row        = vec_index(rows, cursor->cr_row);
            row->len += len;
            /* Remove the coalesced row */
            vec_remove(rows, cursor->cr_row + 1, NULL);
        }

        /* Clear everything infront and below the cursor */
        size_t cap = sizeof(clrscrd clrlneol sv_cur_pos ld_cur_pos) + n + sizeof(byte);
        byte   buff[cap];
        buff[0] = '\0';

        terminal.tm_rawterm.c_oflag |= (OPOST);
        settmode(&terminal.tm_rawterm);

        write_or_die(
            buff,
            sprintf(
                buff,
                clrlneol clrscrd sv_cur_pos "%.*s" ld_cur_pos,
                (int32_t) n,
                row->start + cursor->cr_col));

        terminal.tm_rawterm.c_oflag &= ~(OPOST);
        settmode(&terminal.tm_rawterm);
    }
}

void inbuff_redraw(inbuff_t* buffer)
{
    cursor_t* cursor = &buffer->in_cur;
    vec_t*    rows   = buffer->in_rows;
    size_t    ridx   = vec_len(rows) - 1;
    size_t    up     = 0;
    int64_t   temp;

    row_t* row = vec_index(rows, ridx);
    /* Move up until we find the current row */
    for(; ridx != cursor->cr_row; row = vec_index(rows, --ridx)) {
        up++;
        temp = row->len - 1;
        /* Make sure we compensate for multiple terminal
         * rows in a single buffer row */
        while(temp >= (int64_t) terminal.tm_columns) {
            up++;
            temp -= terminal.tm_columns;
        }
    }

    bool   first_row  = cursor->cr_row == 0;
    size_t cursor_col = cursor->cr_col + ((first_row) ? PLEN : 0);
    size_t row_len    = row->len + ((first_row) ? PLEN : 0);
    /* Find the correct terminal row in the current buffer row */
    up = up + (((row_len - 1) / terminal.tm_columns) - (cursor_col / terminal.tm_columns));

    /* Initialize the buffer */
    size_t cap = sizeof(hide_cur show_cur mv_cur_up() mv_cur_col() "++") + (2 * UINT_DIGITS) + buffer->in_len
                 + sizeof(byte);
    byte  buff[cap + 10];
    byte* target = buff;
    buff[0]      = '\0';

    target += sprintf(target, hide_cur "%.*s", (int32_t) buffer->in_len, buffer->in_buffer);
    if(up > 0) target += sprintf(target, CSI "%luA", up);
    target += sprintf(target, CSI "%uG" show_cur, terminal.tm_col);

    terminal.tm_rawterm.c_oflag |= (OPOST);
    settmode(&terminal.tm_rawterm);

    write_or_die(buff, target - buff);

    terminal.tm_rawterm.c_oflag &= ~(OPOST);
    settmode(&terminal.tm_rawterm);
}

void inbuff_clear(inbuff_t* buffer)
{
    vec_clear_capacity(buffer->in_rows, NULL);

    if(__glibc_unlikely(!vec_push(
           buffer->in_rows,
           &(row_t){
               .len   = 0,
               .start = buffer->in_buffer,
           })))
    {
        exit(EXIT_FAILURE);
    }

    buffer->in_len        = 0;
    buffer->in_cur.cr_col = 0;
    buffer->in_cur.cr_row = 0;
}

static bool inbuff_cursor_left(inbuff_t* buffer)
{
    cursor_t* cursor = &buffer->in_cur;
    uint16_t  maxcol = terminal.tm_columns;
    byte      buff[sizeof(CSI CSI "ADG+") + (2 * UINT_DIGITS)];
    buff[0] = '\0';

    if(cursor->cr_col > 0) {
        --cursor->cr_col;

        if(terminal.tm_col == 1) {
            terminal.tm_col = maxcol;
            write_or_die(buff, sprintf(buff, CSI "%uA" CSI "%uG", 1, maxcol));
        } else {
            terminal.tm_col--;
            write_or_die(mv_cur_left(1), sizeof(mv_cur_left(1)));
        }

        return true;
    } else if(cursor->cr_row > 0) {
        row_t* row     = vec_index(buffer->in_rows, --cursor->cr_row);
        cursor->cr_col = row->len - 1;

        if(cursor->cr_row == 0) {
            terminal.tm_col = remainder(row->len + PLEN - 1, maxcol) + 1;
        } else {
            terminal.tm_col = remainder(row->len - 1, maxcol) + 1;
        }

        write_or_die(buff, sprintf(buff, CSI "%uA" CSI "%uG", 1, terminal.tm_col));
        return true;
    }

    return false;
} // OK

static bool inbuff_cursor_right(inbuff_t* buffer)
{
    cursor_t* cursor   = &buffer->in_cur;
    row_t*    row      = vec_index(buffer->in_rows, cursor->cr_row);
    size_t    nrow     = vec_len(buffer->in_rows);
    uint16_t  maxcol   = terminal.tm_columns;
    bool      last_row = cursor->cr_row == nrow - 1;

    if(cursor->cr_col < row->len - ((last_row) ? 0 : 1)) {
        ++cursor->cr_col;

        if(terminal.tm_col < maxcol) {
            terminal.tm_col++;
            redraw_terminal_cursor(CDIR_RIGHT, 0, 1);
        } else {
            terminal.tm_col = 1;
            redraw_terminal_cursor(CDIR_DOWN | CDIR_ABSC, 1, 1);
        }

        return true;
    } else if(nrow > 0 && cursor->cr_row < (nrow - 1)) {
        row             = vec_index(buffer->in_rows, ++cursor->cr_row);
        cursor->cr_col  = 0;
        terminal.tm_col = 1;
        redraw_terminal_cursor(CDIR_DOWN | CDIR_ABSC, 1, 1);

        return true;
    }

    return false;
} // OK

static bool inbuff_cursor_up(inbuff_t* buffer)
{
    uint16_t  maxcol = terminal.tm_columns;
    uint16_t  last_terminal_column_up;
    vec_t*    rows = buffer->in_rows;
    row_t*    row;
    cursor_t* cursor = &buffer->in_cur;
    bool      redraw = false;

    /* ------------------------------
     * If this is not the prompt row
     * ------------------------------ */
    if(cursor->cr_row > 0) {
        /* --------------------------------------------------
         * If column row contains multiple terminal rows and
         * current column is not the first terminal row.
         * -------------------------------------------------- */
        if(cursor->cr_col >= maxcol) {
            cursor->cr_col -= maxcol;
            redraw = true;
        }
        /* --------------------------------------------------
         * If current column is in the first terminal row.
         * -------------------------------------------------- */
        else
        {
            row              = vec_index(rows, --cursor->cr_row);
            bool   first_row = cursor->cr_row == 0;
            size_t row_len   = row->len + ((first_row) ? PLEN : 0);

            /* --------------------------------------
             * If row contains multiple terminal rows
             * -------------------------------------- */
            if(row_len >= maxcol) {
                last_terminal_column_up = remainder(row_len - 1, maxcol) + 1;

                if(last_terminal_column_up - 1 >= cursor->cr_col) {
                    cursor->cr_col = cursor->cr_col + (row->len - last_terminal_column_up);
                } else {
                    cursor->cr_col  = row_len - 1;
                    terminal.tm_col = last_terminal_column_up;
                }
                redraw = true;
            }
            /* --------------------------------------
             * If row only has a single terminal row
             * -------------------------------------- */
            else
            {
                if(first_row) {
                    if(cursor->cr_col <= PLEN) {
                        cursor->cr_col  = 0;
                        terminal.tm_col = PLEN + 1;
                    } else {
                        cursor->cr_col -= PLEN;
                    }
                } else {
                    if(row->len == 0) {
                        cursor->cr_col  = 0;
                        terminal.tm_col = 1;
                    } else if(cursor->cr_col >= row->len - 1) {
                        cursor->cr_col  = row->len - 1;
                        terminal.tm_col = row->len;
                    }
                }
                redraw = true;
            }
        }
    }
    /* --------------------------------------------
     * If this is the first row and it contains
     * multiple terminal rows but current cursor
     * column is not in the first terminal row.
     * -------------------------------------------- */
    else if(cursor->cr_col + PLEN >= maxcol)
    {
        if(cursor->cr_col + PLEN - maxcol < maxcol) {
            if(remainder((PLEN + cursor->cr_col), maxcol) < PLEN) {
                cursor->cr_col  = 0;
                terminal.tm_col = PLEN + 1;
            } else {
                cursor->cr_col -= maxcol;
            }
        } else {
            cursor->cr_col -= maxcol;
        }
        redraw = true;
    }

    if(redraw) {
        byte buff[sizeof(CSI CSI "AG++") + (2 * UINT_DIGITS)];
        buff[0] = '\0';
        write_or_die(buff, sprintf(buff, CSI "%uA" CSI "%uG", 1, terminal.tm_col));
    }

    return redraw;
} // OK

static bool inbuff_cursor_down(inbuff_t* buffer)
{
    uint16_t  maxcol         = terminal.tm_columns;
    vec_t*    rows           = buffer->in_rows;
    cursor_t* cursor         = &buffer->in_cur;
    uint16_t  last_row       = vec_len(rows) - 1;
    row_t*    row            = vec_index(rows, cursor->cr_row);
    size_t    prompt_row_len = row->len + PLEN;
    size_t    prompt_cur_col = cursor->cr_col + PLEN;
    bool      redraw         = false;

    /* -------------------------------
     * If this is not the prompt row
     * ------------------------------- */
    if(cursor->cr_row > 0) {
        /* ---------------------------------------------------
         * If column should be in the same buffer row
         * --------------------------------------------------- */
        if(row->len > cursor->cr_col && (cursor->cr_col / maxcol) < ((row->len - 1) / maxcol)) {

            if(row->len - 1 - maxcol >= cursor->cr_col) {
                cursor->cr_col += maxcol;
            } else {
                cursor->cr_col  = row->len - 1;
                terminal.tm_col = remainder(row->len - 1, maxcol) + 1;
            }

            redraw = true;
        }
        /* ------------------------------------------------
         * If column should be in buffer row below
         * ------------------------------------------------ */
        else if(cursor->cr_row < last_row)
        {
            row = vec_index(rows, ++cursor->cr_row);
            uint16_t col_modulo;

            if(row->len >= maxcol) {
                cursor->cr_col = remainder(cursor->cr_col, maxcol);
            } else if(row->len >= (col_modulo = remainder(cursor->cr_col, maxcol))) {
                cursor->cr_col = col_modulo;
            } else {
                cursor->cr_col  = row->len - 1;
                terminal.tm_col = row->len;
            }

            redraw = true;
        }
    }
    /* --------------------------------------------------------
     * If this is prompt row and it has multiple terminal rows.
     * -------------------------------------------------------- */
    else if(prompt_row_len > prompt_cur_col && (prompt_cur_col / maxcol) < ((prompt_row_len - 1) / maxcol))
    {
        if(prompt_row_len - 1 - maxcol >= prompt_cur_col
           || remainder(prompt_row_len - 1, maxcol) >= remainder(prompt_cur_col, maxcol))
        {
            cursor->cr_col += maxcol;
        } else {
            cursor->cr_col  = row->len - 1;
            terminal.tm_col = remainder(prompt_row_len - 1, maxcol) + 1;
        }
        redraw = true;
    }
    /* ------------------------------------------------------------
     * If this is prompt row and at the same time not the last row
     * ------------------------------------------------------------ */
    else if(0 < last_row)
    {
        row = vec_index(rows, ++cursor->cr_row);

        if(__glibc_unlikely(row->len == 0)) {
            cursor->cr_col  = 0;
            terminal.tm_col = 1;
        } else if(row->len >= maxcol || row->len - 1 >= remainder(prompt_cur_col, maxcol)) {
            cursor->cr_col = remainder(prompt_cur_col, maxcol);
        } else {
            cursor->cr_col  = row->len - 1;
            terminal.tm_col = row->len;
        }

        redraw = true;
    }

    if(redraw) {
        byte buff[sizeof(CSI CSI "BG++") + (2 * UINT_DIGITS)];
        buff[0] = '\0';
        write_or_die(buff, sprintf(buff, CSI "%uB" CSI "%uG", 1, terminal.tm_col));
    }

    return redraw;
}

void goto_eol(inbuff_t* buffer)
{
    cursor_t* cursor         = &buffer->in_cur;
    uint16_t  maxcol         = terminal.tm_columns;
    uint16_t  current_row    = cursor->cr_row;
    uint16_t  current_column = cursor->cr_col + ((current_row == 0) ? PLEN : 0);
    row_t*    row            = vec_index(buffer->in_rows, current_row);
    size_t    row_len        = row->len + ((current_row == 0) ? PLEN : 0);

    if(row->len > 0 && (remainder(current_column, maxcol) != maxcol - 1 || cursor->cr_col != row->len - 1)) {

        if((current_column / maxcol) < ((row_len - 1) / maxcol)) {
            terminal.tm_col = maxcol;
            cursor->cr_col += (maxcol - 1 - remainder(current_column, maxcol));
        } else {
            terminal.tm_col = row_len % maxcol;
            cursor->cr_col  = row->len - 1;
        }

        byte out[sizeof(mv_cur_col() "+") + UINT_DIGITS];
        out[0] = '\0';
        write_or_die(out, sprintf(out, CSI "%uG", terminal.tm_col));
    }
}

void goto_home(inbuff_t* buffer)
{
    cursor_t* cursor         = &buffer->in_cur;
    uint16_t  maxcol         = terminal.tm_columns;
    uint16_t  current_row    = cursor->cr_row;
    uint16_t  current_column = cursor->cr_col + ((current_row == 0) ? PLEN : 0);

    if(current_column % maxcol != 0) {
        if(current_column < maxcol) {
            cursor->cr_col  = 0;
            terminal.tm_col = ((cursor->cr_row == 0) ? PLEN : 0) + 1;
        } else {
            cursor->cr_col -= remainder(current_column, maxcol);
            terminal.tm_col = 1;
        }

        byte out[sizeof(CSI "G+") + UINT_DIGITS];
        out[0] = '\0';
        write_or_die(out, sprintf(out, CSI "%uG", terminal.tm_col));
    }
}

static void clear_screen(inbuff_t* buffer)
{
    write_or_die(hide_cur mv_cur_home clrscr, sizeof(hide_cur clrscr mv_cur_home));
    pprompt();
    inbuff_redraw(buffer);
}

static termkey_t read_key(void)
{
    int  nread;
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

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return ESCAPE;
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return ESCAPE;

        if(seq[0] == '[') {
            if(isdigit(seq[1])) {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return ESCAPE;
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

__attribute__((unused)) static void inbuff_debug_print(inbuff_t* buffer)
{
    static const size_t buffsize = 100000;
    static size_t       logn     = 1;

    cursor_t* cursor = &buffer->in_cur;
    vec_t*    rows   = buffer->in_rows;
    size_t    len    = vec_len(rows);
    row_t*    row;

    int fd = open("ashelog", O_CREAT | O_RDWR | O_APPEND, S_IRWXU);

    if(fd < 0) {
        fprintf(stderr, "failed creating log file\n");
        die();
    }

    byte  dbgbuff[buffsize];
    byte* target = dbgbuff;
    dbgbuff[0]   = '\0';

    target += sprintf(target, "\n=====LOG %ld=====\n", logn++);
    target += sprintf(target, "ROWS: %ld\n", len);
    for(size_t i = 0; i < len; i++) {
        row = vec_index(rows, i);
        target += sprintf(
            target, "ROW: '%.*s', LEN: %d\n", (int32_t) row->len - 1, row->start, (int32_t) row->len);
    }
    target += sprintf(target, "Cursor -> row: %u, column: %u\n", cursor->cr_row, cursor->cr_col);
    target += sprintf(target, "\n");

    if(write(fd, dbgbuff, target - dbgbuff) < 0) {
        fprintf(stderr, "failed writing to log file\n");
        die();
    }

    close(fd);
}

static bool inbuff_process_key(inbuff_t* buffer)
{
    termkey_t c;

    c = read_key();

    if((IMPLEMENTED(c))) {
        switch(c) {
            case CR:
                if(inbuff_cr(buffer)) return false;
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
            case CTRL_KEY('h'):
            case CTRL_KEY('x'):
            case CTRL_KEY('j'):
            case CTRL_KEY('k'):
            case CTRL_KEY('i'): break;
            default: inbuff_insert(buffer, c); break;
        }
    }

    return true;
}

void inbuff_goto_end(inbuff_t* buffer)
{
    while(inbuff_cursor_down(buffer))
        ;
    goto_eol(buffer);
}

void read_input(inbuff_t* buffer)
{
    /* Set reading flag in case we get interrupted */
    terminal.tm_reading = true;
    fflush(stderr);
    /* Set raw mode */
    settmode(&terminal.tm_rawterm);
    /* Get terminal size */
    get_size_or_die(&terminal.tm_rows, &terminal.tm_columns);
    /* Get terminal cursor position (column) */
    get_cursor_pos(NULL, &terminal.tm_col);
    /* Loop until newline */
    while(inbuff_process_key(buffer)) {
        unblock_signals(); /* Unblock signals while waiting for input */
    }
    /* Null-Terminate the buffer */
    buffer->in_buffer[buffer->in_len++] = '\0';
    inbuff_goto_end(buffer);
    fprintf(stderr, "\r\n");
    fflush(stderr);
    /* Unset the reading flag */
    terminal.tm_reading = false;
    /* Set default shell terminal mode */
    settmode(&terminal.tm_dflterm);
    /* Unblock signals */
    unblock_signals();
}
