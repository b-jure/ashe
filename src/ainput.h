#ifndef AINPUT_H
#define AINPUT_H

#include "acommon.h"
#include "aarray.h"
#include "atoken.h"

#include <termios.h>

#define A_TM	  ashe.sh_term
#define A_TI	  ashe.sh_term.tm_input
#define A_IBF	  A_TI.in_ibf
#define A_DBF	  A_TI.in_dbf
#define A_LINES	  A_TI.in_lines
#define A_IBFIDX  A_TI.in_ibfidx
#define A_ROW	  A_TI.in_cursor.cr_row
#define A_COL	  A_TI.in_cursor.cr_col
#define A_LINE	  A_LINES.data[A_ROW]
#define A_TCOL	  ashe.sh_term.tm_col
#define A_TCOLMAX ashe.sh_term.tm_columns
#define A_PLEN	  ashe.sh_term.tm_promptlen

#define a_terminput_clear() (a_terminput_free(), a_terminput_init())

#define ashe_get_winsize_or_panic(row, col)                              \
	do {                                                             \
		if (unlikely(ashe_get_winsize((row), (col)) < 0))        \
			ashe_panic("couldn't get terminal window size"); \
	} while (0)

struct a_cursor {
	uint32 cr_col; /* current column */
	uint32 cr_row; /* current row */
}; /* current Line position */

struct a_line {
	char *start; /* start of line */
	memmax len; /* line length (bytes) */
}; /* slice of bytes */

ARRAY_NEW(a_arr_line, struct a_line)

struct a_terminput {
	a_arr_char in_ibf; /* input buffer */
	a_arr_char in_dbf; /* draw buffer */
	a_arr_line
		in_lines; /* abstract input newline or terminal wrapping as lines */
	struct a_cursor in_cursor; /* terminal cursor */
	memmax in_ibfidx; /* input buffer index */
};

void a_terminput_init(void);
void a_terminput_read(void);
void a_terminput_free(void);

struct a_term {
	struct a_terminput tm_input;
	struct termios tm_dfltermios; /* default mode */
	struct termios tm_rawtermios; /* raw mode */
	memmax tm_promptlen; /* prompt length */
	uint32 tm_rows; /* terminal rows */
	uint32 tm_columns; /* terminal columns */
	uint32 tm_col; /* current terminal column */
	ubyte tm_reading; /* flag indicating if we are reading input */
};

void a_term_init(void);
#define a_term_free() a_terminput_free()

/* public only for signal handlers (SIGCHLD and SIGIWNCH) */
int32 ashe_get_winsize(uint32 *height, uint32 *width);
int32 ashe_get_curpos(uint32 *row, uint32 *col);

/* === public API === */
ubyte ashe_insert(int32 c);
ubyte ashe_remove(void);
ubyte ashe_cr(void);
void ashe_redraw(void);
void ashe_clear_screen(void);
void ashe_clear_screen_raw(void);
ubyte ashe_cursor_left(void);
ubyte ashe_cursor_right(void);
ubyte ashe_cursor_down(void);
ubyte ashe_cursor_up(void);
ubyte ashe_cursor_lineend(void);
ubyte ashe_cursor_linestart(void);
void ashe_cursor_end(void);

#endif
