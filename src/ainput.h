#ifndef AINPUT_H
#define AINPUT_H

#include "acommon.h"
#include "aarray.h"
#include "atoken.h"

#include <termios.h>


#define INSIZE 200
#define MAXNAME 1024 /* Maximum size of username */


#define CSI "\033["
#define ESC(seq) CSI #seq


#define write_or_die(ptr, n)                                                                       \
    do {                                                                                           \
        if(write(STDERR_FILENO, ptr, n) == -1) die();                                              \
        fflush(stderr);                                                                            \
    } while(0)

#define get_size_or_die(row, col)                                                                  \
    do {                                                                                           \
        if(unlikely(                                                                               \
               get_window_size((row), (col)) == -1 &&                                         \
               get_window_size_fallback((row), (col)) == -1))                                 \
            die();                                                                                 \
    } while(0)

/*------------- CURSOR ----------------*/
#define mv_cur_home ESC(H)
#define mv_cur_left(n) ESC(n) "D"
#define mv_cur_right(n) ESC(n) "C"
#define mv_cur_lastcol ESC(9999) "C"
#define mv_cur_up(n) ESC(n) "A"
#define mv_cur_down(n) ESC(n) "B"
#define mv_cur_lastrow ESC(9999) "B"
#define mv_cur_col(col) ESC(col) "G"
#define sv_cur_pos ESC(s)
#define ld_cur_pos ESC(u)
#define mv_cur_pos(row, col) ESC(row ";" col) "H"
#define request_cur_pos ESC(6n)
#define hide_cur ESC(?25l)
#define show_cur ESC(?25h)
/*-------------------------------------*/
/*
 *
 */
/*-------------- ERASE ----------------*/
#define clrscrd ESC(0J)
#define clrscru ESC(1J)
#define clrscr ESC(2J)
#define clrlneol ESC(0K)
#define clrlnsol ESC(1K)
#define clrln ESC(2K)
/*-------------------------------------*/
/*
 *
 */
/*-------------- MODES ----------------*/
#define bold(text) ESC(1m) text ESC(22m)
#define italic(text) ESC(3m) text ESC(23m)
#define blink(text) ESC(5m) text ESC(25m)
/*-------------------------------------*/
/*
 *
 */
/*-------------- COLORS ---------------*/
#define magenta(text) ESC(35m) text ESC(0m)
#define yellow(text) ESC(33m) text ESC(0m)
#define byellow(text) ESC(93m) text ESC(0m)
#define green(text) ESC(32m) text ESC(0m)
#define red(text) ESC(31m) text ESC(0m)
// Bright colors
#define bred(text) ESC(91m) text ESC(0m)
#define cyan(text) ESC(36m) text ESC(0m)
#define blue(text) ESC(34m) text ESC(0m)
#define bblue(text) ESC(94m) text ESC(0m)
#define bwhite(text) ESC(97m) text ESC(0m)
#define byellow(text) ESC(93m) text ESC(0m)
/*-------------------------------------*/
/*
 *
 */
/*-------------- CUSTOM ---------------*/
#define obrack cyan("[")
#define cbrack cyan("]")
#define bracketed(text) obrack text cbrack
/*-------------------------------------*/


typedef struct {
    uint32 cr_col; /* current column */
    uint32 cr_row; /* current row */
} Cursor;


typedef struct {
    char* start; /* start of line */
    memmax len; /* line length (bytes) */
} Line;

ARRAY_NEW(ArrayLine, Line);


typedef struct {
    Buffer in_buffer; /* input buffer */
    Buffer in_dbuffer; /* draw buffer */
    ArrayLine in_lines; /* abstract input newline or terminal wrapping as lines */
    Cursor in_cursor; /* terminal cursor */
} TerminalInput;

void TerminalInput_clear(TerminalInput* tinput);
void TerminalInput_read(TerminalInput* tinput);
void TerminalInput_redraw(TerminalInput* tinput);
void TerminalInput_gotoend(TerminalInput* tinput);



typedef struct {
    TerminalInput tm_input;
    struct termios tm_dfltermios; /* default mode */
    struct termios tm_rawtermios; /* raw mode */
    uint32 tm_rows; /* terminal rows */
    uint32 tm_columns; /* terminal columns */
    uint32 tm_col; /* current terminal column */
    memmax tm_promptlen; /* prompt length */
    ubyte tm_reading; /* flag indicating if we are reading input */
} Terminal;

void Terminal_init(Terminal* term);
void Terminal_free(Terminal* term);

void print_prompt(void);

int32 get_window_size_fallback(uint32* height, uint32* width);
int32 get_window_size(uint32* height, uint32* width);
int32 get_cursor_pos(uint32* row, uint32* col);

#endif
