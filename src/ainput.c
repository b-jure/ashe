#include "autils.h"
#include "aasync.h"
#include "aerrors.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "ashell.h"

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#undef inbuff /* Don't need it here */


/*------------------------- PROMPT ------------------------*/
#if !defined(ASHE_PROMPT_LEN_MAX)
#define ASHE_PROMPT_LEN_MAX ((ARG_MAX >> 4) ? (ARG_MAX >> 4) : 1024)
#endif

#if !defined(ASHE_PROMPT)
#define ASHE_PROMPT "$: "
#endif

#define ASH_P_JOBS bold(byellow("%ld"))
#define ASH_P_PREFIX bold(bred("%s"))
#define ASH_P_USER bold(magenta("%s"))
#define ASH_P_SEP bold(cyan("%s"))
#define ASH_P_SYSTEM bold(bwhite("%s"))
#define ASH_P_SUFFIX bwhite("%s")

#define ASH_P_DEFAULT                                                                              \
    bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM) ASH_P_SUFFIX
#define ASH_P_WJOBCOUNT                                                                            \
    bracketed(ASH_P_JOBS) bracketed(ASH_P_PREFIX) bracketed(ASH_P_USER ASH_P_SEP ASH_P_SYSTEM)     \
        ASH_P_SUFFIX
/*---------------------------------------------------------*/



// TODO: remove this piece of crap maybe?
#define CDIR_DOWN 1
#define CDIR_UP 2
#define CDIR_LEFT 4
#define CDIR_RIGHT 8
#define CDIR_ABSC 16
#define CDIR_ABS 32
#define redraw_terminal_cursor(CDIR, row, col)                                                     \
    do {                                                                                           \
        switch(CDIR) {                                                                             \
            case CDIR_DOWN: write_or_die(mv_cur_down(row), sizeof(mv_cur_down(row))); break;       \
            case CDIR_UP: write_or_die(mv_cur_up(row), sizeof(mv_cur_up(row))); break;             \
            case CDIR_LEFT: write_or_die(mv_cur_left(col), sizeof(mv_cur_left(col))); break;       \
            case CDIR_RIGHT: write_or_die(mv_cur_right(col), sizeof(mv_cur_right(col))); break;    \
            case(CDIR_UP | CDIR_LEFT):                                                             \
                write_or_die(                                                                      \
                    mv_cur_up(row) mv_cur_left(col),                                               \
                    sizeof(mv_cur_up(row) mv_cur_left(col)));                                      \
                break;                                                                             \
            case(CDIR_UP | CDIR_RIGHT):                                                            \
                write_or_die(                                                                      \
                    mv_cur_up(row) mv_cur_right(col),                                              \
                    sizeof(mv_cur_up(row) mv_cur_right(col)));                                     \
                break;                                                                             \
            case(CDIR_DOWN | CDIR_LEFT):                                                           \
                write_or_die(                                                                      \
                    mv_cur_down(row) mv_cur_left(col),                                             \
                    sizeof(mv_cur_down(row) mv_cur_left(col)));                                    \
                break;                                                                             \
            case(CDIR_DOWN | CDIR_RIGHT):                                                          \
                write_or_die(                                                                      \
                    mv_cur_down(row) mv_cur_right(col),                                            \
                    sizeof(mv_cur_down(row) mv_cur_right(col)));                                   \
                break;                                                                             \
            case(CDIR_DOWN | CDIR_ABSC):                                                           \
                write_or_die(                                                                      \
                    mv_cur_down(row) mv_cur_col(col),                                              \
                    sizeof(mv_cur_down(row) mv_cur_col(col)));                                     \
                break;                                                                             \
            case(CDIR_UP | CDIR_ABSC):                                                             \
                write_or_die(                                                                      \
                    mv_cur_up(row) mv_cur_col(col),                                                \
                    sizeof(mv_cur_up(row) mv_cur_col(col)));                                       \
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
} termkeytag;

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESCAPE 27
#define CR 0x0D

#define IMPLEMENTED(c) (c != ESCAPE)
#define remainder(x, y) ((x) % (y))

typedef int32 termkey;



static int32 get_window_size(uint32* height, uint32* width)
{
    struct winsize ws;
    if(unlikely(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0)) {
        return get_window_size_fallback(height, width);
    } else {
        if(height) *height = ws.ws_row;
        if(width) *width = ws.ws_col;
        return 0;
    }
}


int32 get_window_size_fallback(uint32* height, uint32* width)
{
    memmax n = sizeof(sv_cur_pos mv_cur_right(99999) mv_cur_down(99999) request_cur_pos ld_cur_pos);
    write_or_die(sv_cur_pos mv_cur_right(99999) mv_cur_down(99999) request_cur_pos ld_cur_pos, n);
    return get_cursor_pos(height, width);
}


static void expand_pp(Buffer* buffer, char* sysname, char* username, uint32 jobcount)
{
    static const int32 delimiter = '%';

    char* base = buffer->data;
    char* ptr = base;
    char* start = NULL;
    char* end = NULL;
    const byte* pholder = NULL;

    // TODO: Implement
}


void pprompt(void)
{
    static char username[MAXNAME] = {0};
    static char prompt[ASHE_PROMPT_LEN_MAX] = {0};
    struct passwd pwd;
    struct utsname system;

    memmax jobn = JobControl_jobs(&ashe.sh_jobcntl);

    if(unlikely(uname(&system) < 0)) {
        /* TODO: errmsg */
        die();
    } else if(unlikely(getlogin_r(username, MAXNAME) < 0)) {
        /* TODO: errmsg */
        die();
    }

    // TODO: Implement
}


/* Auxiliary to 'Terminal_init()' */
static void init_rawterm(struct termios* rawterm)
{
    tcgetattr(STDIN_FILENO, rawterm);
    rawterm->c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    rawterm->c_oflag &= ~(OPOST);
    rawterm->c_lflag &= ~(ECHO | ICANON | IEXTEN);
    rawterm->c_cflag |= (CS8);
    rawterm->c_cc[VMIN] = 1;
    rawterm->c_cc[VTIME] = 0;
}


/* Auxiliary to 'Terminal_init()' */
static finline void init_dflterm(struct termios* dflterm)
{
    tcgetattr(STDIN_FILENO, dflterm);
}


static void TerminalInput_init(TerminalInput* tinput)
{
    Buffer_init(&tinput->in_buffer);
    Buffer_init(&tinput->in_dbuffer);
    ArrayLine_init(&tinput->in_lines);
}


static void TerminalInput_free(TerminalInput* tinput)
{
    Buffer_free(&tinput->in_buffer, NULL);
    Buffer_free(&tinput->in_dbuffer, NULL);
    ArrayLine_free(&tinput->in_lines, NULL);
}


void Terminal_init(Terminal* term)
{
    TerminalInput_init(&term->tm_input);
    init_rawterm(&term->tm_rawtermios);
    init_dflterm(&term->tm_dfltermios);
    get_size_or_die(&term->tm_rows, &term->tm_columns);
    // tm_col - gets set when reading and drawing
    // tm_promptlen - gets set in 'print_prompt()'
    term->tm_reading = 0;
}


void Terminal_free(Terminal* term)
{
    TerminalInput_free(&term->tm_input);
}


static finline void set_terminal_mode(struct termios* tmode)
{
    if(unlikely(tcsetattr(STDIN_FILENO, TCSAFLUSH, tmode) < 0)) die();
}


int32 get_cursor_pos(uint32* row, uint32* col)
{
    int32 c;
    memmax i = 0;
    int32 nread;
    memmax srow, scol;
    const char* fmt = "\033[%zu;%zu";
    char buf[UINT_DIGITS * 2 + sizeof(fmt)];

    write_or_die(request_cur_pos, sizeof(request_cur_pos));

    while((nread = read(STDIN_FILENO, &c, 1)) == 1) {
        if(c == 'R') break;
        buf[i++] = c;
    }

    if(nread == -1) {
        print_errno();
        return -1;
    }

    sscanf(buf, fmt, &srow, &scol);

    if(row) *row = srow;
    if(col) *col = scol;

    return 0;
}


static void shift_lines_right(TerminalInput* tinput, memmax start)
{
    ArrayLine* lines = &tinput->in_lines;
    memmax len = lines->len;
    for(memmax i = start; i < len; i++)
        ((Line*)&lines->data[i])->start++;
}

static void shift_lines_left(TerminalInput* tinput, memmax start)
{
    ArrayLine* lines = &tinput->in_lines;
    memmax len = lines->len;
    for(memmax i = start; i < len; i++)
        ((Line*)&lines->data[i])->start--;
}

static memmax relative_input_length(TerminalInput* tinput)
{
    memmax len = 0;
    ArrayLine* lines = &tinput->in_lines;
    for(memmax i = 0; i < tinput->in_cursor.cr_row; i++)
        len += lines->data[i].len;
    len += tinput->in_cursor.cr_col;
    return len;
}


static finline Line* current_line(TerminalInput* tinput)
{
    return &tinput->in_lines.data[tinput->in_cursor.cr_row];
}


static void inbuff_insert(TerminalInput* tinput, int32 c)
{
    memmax len = tinput->in_buffer.len;
    if(len < ARG_MAX - 1) {
        memmax maxcol = ashe.sh_term.tm_columns;
        Cursor* cursor = &tinput->in_cursor;
        Line* line = current_line(tinput);
        memmax lenrel = relative_input_length(tinput);
        memmax n = tinput->in_len - lenrel;
        char* src = line->start + cursor->cr_col;

        memmove(src + 1, src, n);
        *src = c;
        shift_lines_right(tinput, cursor->cr_row + 1);
        line->len++;

        memmax cap = n + sizeof("\r\n" clrlneol clrscrd) + 10;
        byte buff[cap];
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
        tinput->in_len++;

        strcat(buff, sv_cur_pos);
        strncat(buff, line->start + cursor->cr_col, n);
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


static ubyte cr(TerminalInput* tinput)
{
    Cursor* cursor = &tinput->in_cursor;
    ArrayLine* lines = &tinput->in_lines;
    Line* odlline = &lines->data[cursor->cr_row];

    if(is_escaped(odlline->start, cursor->cr_col) ||
       in_dq(tinput->in_buffer, relative_input_length(tinput)))
    {
        inbuff_insert(tinput, '\n');

        Row new_row = {
            .start = old_row->start + cursor->cr_col,
            .len = old_row->len - cursor->cr_col,
        };

        odlline->len = cursor->cr_col;

        terminal.tm_col = 1;
        cursor->cr_col = 0;
        vec_insert(rows, &new_row, ++cursor->cr_row);

        return false;
    } else {
        return true;
    }
}

static void inbuff_remove(TerminalInput* buffer)
{
    Cursor* cursor = &buffer->in_cursor;

    if(cursor->cr_col > 0 || cursor->cr_row > 0) {
        vec_t* rows = buffer->in_rows;
        Row* row = vec_index(rows, cursor->cr_row);
        ubyte coalesce = false;

        memmax lenrel = relative_input_length(buffer);
        memmax n = buffer->in_len - lenrel;
        byte* src = row->start + cursor->cr_col;
        memmove(src - 1, src, n);
        shift_lines_left(buffer, cursor->cr_row + 1);
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
            memmax len = row->len;
            row = vec_index(rows, cursor->cr_row);
            row->len += len;
            /* Remove the coalesced row */
            vec_remove(rows, cursor->cr_row + 1, NULL);
        }

        /* Clear everything infront and below the cursor */
        memmax cap = sizeof(clrscrd clrlneol sv_cur_pos ld_cur_pos) + n + sizeof(byte);
        byte buff[cap];
        buff[0] = '\0';

        terminal.tm_rawterm.c_oflag |= (OPOST);
        settmode(&terminal.tm_rawterm);

        write_or_die(
            buff,
            sprintf(
                buff,
                clrlneol clrscrd sv_cur_pos "%.*s" ld_cur_pos,
                (int32_t)n,
                row->start + cursor->cr_col));

        terminal.tm_rawterm.c_oflag &= ~(OPOST);
        settmode(&terminal.tm_rawterm);
    }
}

void inbuff_redraw(TerminalInput* buffer)
{
    Cursor* cursor = &buffer->in_cursor;
    vec_t* rows = buffer->in_rows;
    memmax ridx = vec_len(rows) - 1;
    memmax up = 0;
    int64_t temp;

    Row* row = vec_index(rows, ridx);
    /* Move up until we find the current row */
    for(; ridx != cursor->cr_row; row = vec_index(rows, --ridx)) {
        up++;
        temp = row->len - 1;
        /* Make sure we compensate for multiple terminal
         * rows in a single buffer row */
        while(temp >= (int32_t)terminal.tm_columns) {
            up++;
            temp -= terminal.tm_columns;
        }
    }

    ubyte first_row = cursor->cr_row == 0;
    memmax cursor_col = cursor->cr_col + ((first_row) ? PLEN : 0);
    memmax row_len = row->len + ((first_row) ? PLEN : 0);
    /* Find the correct terminal row in the current buffer row */
    up = up + (((row_len - 1) / terminal.tm_columns) - (cursor_col / terminal.tm_columns));

    /* Initialize the buffer */
    memmax cap = sizeof(hide_cur show_cur mv_cur_up() mv_cur_col() "++") + (2 * UINT_DIGITS) +
                 buffer->in_len + sizeof(byte);
    byte buff[cap + 10];
    byte* target = buff;
    buff[0] = '\0';

    target += sprintf(target, "%s%.*s", hide_cur, (int32_t)buffer->in_len, buffer->in_buffer);
    if(up > 0) target += sprintf(target, "%s%luA", CSI, up);
    target += sprintf(target, CSI "%uG" show_cur, terminal.tm_col);

    terminal.tm_rawterm.c_oflag |= (OPOST);
    settmode(&terminal.tm_rawterm);

    write_or_die(buff, target - buff);

    terminal.tm_rawterm.c_oflag &= ~(OPOST);
    settmode(&terminal.tm_rawterm);
}

void inbuff_clear(TerminalInput* buffer)
{
    vec_clear_capacity(buffer->in_rows, NULL);

    if(unlikely(!vec_push(
           buffer->in_rows,
           &(Row){
               .len = 0,
               .start = buffer->in_buffer,
           })))
    {
        exit(EXIT_FAILURE);
    }

    buffer->in_len = 0;
    buffer->in_cursor.cr_col = 0;
    buffer->in_cursor.cr_row = 0;
}

static ubyte inbuff_cursor_left(TerminalInput* buffer)
{
    Cursor* cursor = &buffer->in_cursor;
    uint32 maxcol = terminal.tm_columns;
    byte buff[sizeof(CSI CSI "ADG+") + (2 * UINT_DIGITS)];
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
        Row* row = vec_index(buffer->in_rows, --cursor->cr_row);
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

static ubyte inbuff_cursor_right(TerminalInput* buffer)
{
    Cursor* cursor = &buffer->in_cursor;
    Row* row = vec_index(buffer->in_rows, cursor->cr_row);
    memmax nrow = vec_len(buffer->in_rows);
    uint32 maxcol = terminal.tm_columns;
    ubyte last_row = cursor->cr_row == nrow - 1;

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
        row = vec_index(buffer->in_rows, ++cursor->cr_row);
        cursor->cr_col = 0;
        terminal.tm_col = 1;
        redraw_terminal_cursor(CDIR_DOWN | CDIR_ABSC, 1, 1);

        return true;
    }

    return false;
} // OK

static ubyte inbuff_cursor_up(TerminalInput* buffer)
{
    uint32 maxcol = terminal.tm_columns;
    uint32 last_terminal_column_up;
    vec_t* rows = buffer->in_rows;
    Row* row;
    Cursor* cursor = &buffer->in_cursor;
    ubyte redraw = false;

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
            row = vec_index(rows, --cursor->cr_row);
            ubyte first_row = cursor->cr_row == 0;
            memmax row_len = row->len + (first_row * PLEN);

            /* --------------------------------------
             * If row contains multiple terminal rows
             * -------------------------------------- */
            if(row_len >= maxcol) {
                last_terminal_column_up = remainder(row_len - 1, maxcol) + 1;

                if(last_terminal_column_up - 1 >= cursor->cr_col) {
                    cursor->cr_col = cursor->cr_col + (row->len - last_terminal_column_up);
                } else {
                    cursor->cr_col = row_len - 1;
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
                    if(cursor->cr_col <= row_len - 1) {
                        cursor->cr_col = 0;
                        terminal.tm_col = PLEN + 1;
                    } else {
                        cursor->cr_col = row_len - (PLEN + 1);
                        terminal.tm_col = row_len;
                    }
                } else {
                    if(row->len == 0) {
                        cursor->cr_col = 0;
                        terminal.tm_col = 1;
                    } else if(cursor->cr_col >= row->len - 1) {
                        cursor->cr_col = row->len - 1;
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
                cursor->cr_col = 0;
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

static ubyte inbuff_cursor_down(TerminalInput* buffer)
{
    uint32 maxcol = terminal.tm_columns;
    vec_t* rows = buffer->in_rows;
    Cursor* cursor = &buffer->in_cursor;
    uint32 last_row = vec_len(rows) - 1;
    Row* row = vec_index(rows, cursor->cr_row);
    memmax prompt_row_len = row->len + PLEN;
    memmax prompt_cur_col = cursor->cr_col + PLEN;
    ubyte redraw = false;

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
                cursor->cr_col = row->len - 1;
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
            uint32 col_modulo;

            if(row->len >= maxcol) {
                cursor->cr_col = remainder(cursor->cr_col, maxcol);
            } else if(row->len >= (col_modulo = remainder(cursor->cr_col, maxcol))) {
                cursor->cr_col = col_modulo;
            } else {
                cursor->cr_col = row->len - 1;
                terminal.tm_col = row->len;
            }

            redraw = true;
        }
    }
    /* --------------------------------------------------------
     * If this is prompt row and it has multiple terminal rows.
     * -------------------------------------------------------- */
    else if(
        prompt_row_len > prompt_cur_col &&
        (prompt_cur_col / maxcol) < ((prompt_row_len - 1) / maxcol))
    {
        if(prompt_row_len - 1 - maxcol >= prompt_cur_col ||
           remainder(prompt_row_len - 1, maxcol) >= remainder(prompt_cur_col, maxcol))
        {
            cursor->cr_col += maxcol;
        } else {
            cursor->cr_col = row->len - 1;
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

        if(unlikely(row->len == 0)) {
            cursor->cr_col = 0;
            terminal.tm_col = 1;
        } else if(row->len >= maxcol || row->len - 1 >= remainder(prompt_cur_col, maxcol)) {
            cursor->cr_col = remainder(prompt_cur_col, maxcol);
        } else {
            cursor->cr_col = row->len - 1;
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

void goto_eol(TerminalInput* buffer)
{
    Cursor* cursor = &buffer->in_cursor;
    uint32 maxcol = terminal.tm_columns;
    uint32 current_row = cursor->cr_row;
    uint32 current_column = cursor->cr_col + ((current_row == 0) ? PLEN : 0);
    Row* row = vec_index(buffer->in_rows, current_row);
    memmax row_len = row->len + ((current_row == 0) ? PLEN : 0);

    if(row->len > 0 &&
       (remainder(current_column, maxcol) != maxcol - 1 || cursor->cr_col != row->len - 1))
    {

        if((current_column / maxcol) < ((row_len - 1) / maxcol)) {
            terminal.tm_col = maxcol;
            cursor->cr_col += (maxcol - 1 - remainder(current_column, maxcol));
        } else {
            terminal.tm_col = row_len % maxcol;
            cursor->cr_col = row->len - 1;
        }

        byte out[sizeof(mv_cur_col() "+") + UINT_DIGITS];
        out[0] = '\0';
        write_or_die(out, sprintf(out, CSI "%uG", terminal.tm_col));
    }
}

void goto_home(TerminalInput* buffer)
{
    Cursor* cursor = &buffer->in_cursor;
    uint32 maxcol = terminal.tm_columns;
    uint32 current_row = cursor->cr_row;
    uint32 current_column = cursor->cr_col + ((current_row == 0) ? PLEN : 0);

    if(current_column % maxcol != 0) {
        if(current_column < maxcol) {
            cursor->cr_col = 0;
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

static void clear_screen(TerminalInput* buffer)
{
    write_or_die(hide_cur mv_cur_home clrscr, sizeof(hide_cur clrscr mv_cur_home));
    pprompt();
    inbuff_redraw(buffer);
}

static termkey read_key(void)
{
    int32 nread;
    int32 c;

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
        ubyte seq[3];

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

static ubyte inbuff_process_key(TerminalInput* buffer)
{
    termkey c;

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
            /// Unimplemented
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

void inbuff_goto_end(TerminalInput* buffer)
{
    while(inbuff_cursor_down(buffer));
    goto_eol(buffer);
}

void read_input(TerminalInput* buffer)
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
