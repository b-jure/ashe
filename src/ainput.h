#ifndef AINPUT_H
#define AINPUT_H

#include "acommon.h"
#include "aarray.h"
#include "atoken.h"

#include <termios.h>

#define TM      ashe.sh_term
#define TI	ashe.sh_term.tm_input
#define IBF	TI.in_ibf
#define DBF	TI.in_dbf
#define LINES	TI.in_lines
#define IBFIDX	TI.in_ibfidx
#define ROW	TI.in_cursor.cr_row
#define COL	TI.in_cursor.cr_col
#define LINE	LINES.data[ROW]
#define TCOL	ashe.sh_term.tm_col
#define TCOLMAX ashe.sh_term.tm_columns
#define PLEN	ashe.sh_term.tm_promptlen

#define TerminalInput_clear() (TerminalInput_free(), TerminalInput_init())

#define get_winsize_or_panic(row, col)                               \
	do {                                                         \
		if (unlikely(get_window_size((row), (col)) < 0))     \
			panic("couldn't get terminal window size."); \
	} while (0)

typedef struct {
	uint32 cr_col; /* current column */
	uint32 cr_row; /* current row */
} Cursor; /* current Line position */

typedef struct {
	char *start; /* start of line */
	memmax len; /* line length (bytes) */
} Line; /* slice of bytes */

ARRAY_NEW(ArrayLine, Line)

typedef struct {
	Buffer in_ibf; /* input buffer */
	Buffer in_dbf; /* draw buffer */
	ArrayLine in_lines; /* abstract input newline or terminal wrapping as lines */
	Cursor in_cursor; /* terminal cursor */
	memmax in_ibfidx; /* input buffer index */
} TerminalInput;

void TerminalInput_init(void);
void TerminalInput_read(void);
void TerminalInput_free(void);

typedef struct {
	TerminalInput tm_input;
	struct termios tm_dfltermios; /* default mode */
	struct termios tm_rawtermios; /* raw mode */
	memmax tm_promptlen; /* prompt length */
	uint32 tm_rows; /* terminal rows */
	uint32 tm_columns; /* terminal columns */
	uint32 tm_col; /* current terminal column */
	ubyte tm_reading; /* flag indicating if we are reading input */
} Terminal;

void Terminal_init(void);
#define Terminal_free() TerminalInput_free()

/* public only for signal handlers (SIGCHLD and SIGIWNCH) */
int32 get_window_size(uint32 *height, uint32 *width);
int32 get_cursor_pos(uint32 *row, uint32 *col);

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
