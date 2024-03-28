#ifndef AINPUT_H
#define AINPUT_H

#include "acommon.h"
#include "aarray.h"
#include "atoken.h"

#include <termios.h>

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

#define TerminalInput_clear(tinput) \
	(TerminalInput_free(tinput), TerminalInput_init(tinput))

/*		VT100 ESCAPE SEQUENCES		 */

#define CSI	 "\033["
#define ESC(seq) CSI #seq

#define clrscr() write_or_panic(clrscr cursor_home, sizeof(clrscr cursor_home))

#define write_or_panic(ptr, n)                                    \
	do {                                                      \
		if (unlikely(write(STDERR_FILENO, ptr, n) < 0)) { \
			print_errno();                            \
			panic(NULL);                              \
		}                                                 \
		fflush(stderr);                                   \
		if (unlikely(ferror(stderr))) {                   \
			print_errno();                            \
			panic(NULL);                              \
		}                                                 \
	} while (0)

#define get_winsize_or_panic(row, col)                               \
	do {                                                         \
		if (unlikely(get_window_size((row), (col)) < 0))     \
			panic("couldn't get terminal window size."); \
	} while (0)

/* CURSOR */
#define cursor_home	      ESC(H)
#define cursor_left(n)	      ESC(n) "D"
#define cursor_right(n)	      ESC(n) "C"
#define cursor_up(n)	      ESC(n) "A"
#define cursor_down(n)	      ESC(n) "B"
#define cursor_save	      ESC(s)
#define cursor_load	      ESC(u)
#define cursor_col(col)	      ESC(col) "G"
#define cursor_move(row, col) ESC(row ";" col) "H"
#define cursor_position	      ESC(6n)
#define cursor_hide ESC(?25l)
#define cursor_show ESC(?25h)
/* CLEAR */
#define clear_down	 ESC(0J)
#define clear_up	 ESC(1J)
#define clear_all	 ESC(2J)
#define clear_line_right ESC(0K)
#define clear_line_left	 ESC(1K)
#define clear_line	 ESC(2K)

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
	ubyte in_dq; /* TODO: Implement */
} TerminalInput;

void TerminalInput_init(TerminalInput *tinput);
void TerminalInput_read(TerminalInput *tinput);
void TerminalInput_redraw(TerminalInput *tinput);
void TerminalInput_goto_input_end(TerminalInput *tinput);
void TerminalInput_clrscreen(TerminalInput *tinput);
void TerminalInput_free(TerminalInput *tinput);

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

void Terminal_init(Terminal *term);
void Terminal_free(Terminal *term);

int32 get_window_size(uint32 *height, uint32 *width);
int32 get_cursor_pos(uint32 *row, uint32 *col);

#endif
