#ifndef AINPUT_H
#define AINPUT_H

#include "acommon.h"
#include "aarray.h"
#include "atoken.h"

#include <termios.h>

/* terminal */
#define A_TM ashe.sh_term

/* terminal members */
#define A_TP	  A_TM.tm_prompt
#define A_TPLEN	  (a_arr_len(A_TM.tm_prompt) - 1)
#define A_TDBF	  A_TM.tm_dbf
#define A_TIODFL  A_TM.tm_dfltermios
#define A_TIORAW  A_TM.tm_rawtermios
#define A_TROWMAX A_TM.tm_rows
#define A_TCOLMAX A_TM.tm_columns
#define A_TCOL	  A_TM.tm_col
#define A_TROW	  A_TM.tm_row

/* input */
#define A_TI A_TM.tm_input

/* input members */
#define A_IBF	 A_TI.in_ibf
#define A_IBFIDX A_TI.in_ibfidx
#define A_ILINES A_TI.in_lines
#define A_ICOL	 A_TI.in_col
#define A_IROW	 A_TI.in_row
#define A_ILINE	 a_arr_ptr(A_ILINES)[A_IROW]
#define A_ISROW	 A_TI.in_startrow
#define A_ISCOL	 A_TI.in_startcol

/* slice of bytes */
struct a_line {
	char *start;
	a_memmax len;
};

ARRAY_NEW(a_arr_line, struct a_line)

/* terminal input (private) */
struct a_input {
	/* input buffer and current cursor index within it */
	a_arr_char in_ibf;
	a_uint32 in_ibfidx;

	/* input lines */
	a_arr_line in_lines;
	a_uint32 in_col;
	a_uint32 in_row;

	/* terminal row and col where the input starts */
	a_uint32 in_startrow;
	a_uint32 in_startcol;
};

void a_input_clear(void);

struct a_term {
	/* prompt buffer */
	a_arr_char tm_prompt;

	/* terminal input */
	struct a_input tm_input;

	/* terminal draw buffer */
	a_arr_char tm_dbf;

	/* terminal io */
	struct termios tm_dfltermios;
	struct termios tm_rawtermios;

	/* terminal dimensions */
	a_uint32 tm_rows;
	a_uint32 tm_columns;

	/* cursor position in terminal */
	a_uint32 tm_col;
	a_uint32 tm_row;

	/* set if reading input */
	a_ubyte tm_reading;
};

/* Initialize terminal. */
void a_term_init(void);

/* Free terminal resources. */
void a_term_free(void);

/* Update terminal dimensions. */
void a_term_sync_dimensions(void);

/* Update terminal cursor position. */
void a_term_sync_cursor(void);

/* Start reading from terminal. */
void a_term_read(void);

/*
 * Invoked on SIGWINCH, fixes how input
 * and prompt look by redrawing them correctly.
 */
void sigwinch_redraw(void); // TODO: test

/*
 * ------------------------------------------------------
 * 		TERMINAL INPUT SAFE API
 * 	Safe to invoke always, you can't mess up.
 * 		(but developer can...)
 * ------------------------------------------------------
 */

/*
 * Insert character 'c' under the cursor into the
 * input buffer and update cursor.
 *
 * If the input size limit is reached, character
 * won't get inserted and return value will be 0.
 */
a_ubyte ashe_i_insert(a_ubyte c);

/*
 * Remove character under the cursor from the
 * input buffer and update cursor.
 *
 * In case character was not removed, meaning
 * input buffer was already empty, return value
 * will be 0.
 */
a_ubyte ashe_i_remove(void);

/*
 * Remove 'len' bytes from the input buffer
 * starting from the cursor to the end of the
 * buffer.
 * If buffer is already empty return value is 0.
 *
 * If 'len' is negative, then all of the bytes
 * from the current position of the cursor up
 * to the end of the input buffer are removed.
 * In this case total amount of bytes removed
 * is returned.
 *
 * If 'len' is greater than the buffer size
 * calculated from the current cursor position
 * to the end of the input buffer, return value
 * is 0 and removal is not performed.
 */
a_uint32 ashe_i_remove_bytes(a_ssize len); // TODO: test

/*
 * Insert newline under the cursor and create
 * a new input line.
 *
 * If cursor is not preceeded by the open double
 * quotes and the preceeding character is not escape
 * character then this stops reading from terminal.
 * Meaning shell will try to execute the current
 * input buffer.
 * In this case the function returns 1.
 */
a_ubyte ashe_i_cr(void);

/*
 * Redraw the prompt and clear the input buffer.
 */
void ashe_p_redraw(void);

/*
 * Move terminal cursor once to the left.
 *
 * If cursor is already at the start of
 * the input buffer 0 is returned.
 */
a_ubyte ashe_c_left(void);

/*
 * Move terminal cursor once to the right.
 *
 * If cursor is already at the end of the
 * input buffer 0 is returned.
 */
a_ubyte ashe_c_right(void);

/*
 * Move terminal cursor down once.
 *
 * If cursor is already at the last
 * terminal row of the input 0 is returned.
 */
a_ubyte ashe_c_down(void);

/*
 * Move terminal cursor up once.
 *
 * If cursor is already at the first
 * terminal row of the input 0 is returned.
 */
a_ubyte ashe_c_up(void);

/*
 * Move cursor to the end of the terminal line.
 *
 * If cursor is already at the end of
 * the terminal line 0 is returned.
 */
a_ubyte ashe_c_eol(void);

/*
 * Move cursor to the start of the terminal line.
 *
 * If cursor is already at the start of
 * the terminal line 0 is returned.
 */
a_ubyte ashe_c_sol(void);

/*
 * Clears the screen, redraws the prompt together
 * with the input buffer and sets the terminal cursor
 * to the original position inside the input buffer.
 */
void ashe_s_clear(void);

/*
 * Moves cursor to the start of the input buffer.
 *
 * If the cursor was already at the start this
 * returns 0.
 */
a_ubyte ashe_c_start(void);

/*
 * Moves cursor to the end of the input buffer.
 *
 * If the cursor was already at the start this
 * returns 0.
 */
a_ubyte ashe_c_end(void);

/*
 * ------------------------------------------------------
 * 		TERMINAL INPUT UNSAFE API
 *
 * 	In order to use this effectively without
 * 	breaking the program you need to read
 * 	source code and get familiar with the
 * 	codebase.
 * 	It is up to developer to abstract upon
 * 	this API.
 * 	This API is also exposed for internal
 * 	use in the shell, specifically inside
 * 	the signal handlers and even some of
 * 	the built-in commands have use of it.
 * ------------------------------------------------------
 */

/* read source code :( */
a_ubyte ashe_p_draw_unsafe(void); // TODO: test
void ashe_i_redraw_unsafe(void);
a_ubyte ashe_i_insert_bytes_unsafe(const char *src, a_uint32 len); // TODO: test
void ashe_s_clear_unsafe(void);

#endif
