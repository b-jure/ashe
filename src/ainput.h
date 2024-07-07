/* ----------------------------------------------------------------------------------------------
 * Copyright (C) 2023-2024 Jure Bagić
 *
 * This file is part of ashe.
 * ashe is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ashe is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ashe.
 * If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------------------------*/

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

struct a_line { /* input line */
	char *start;
	a_memmax len;

	/* TODO: implement line flags and trow in order to move
	 * cursor up properly up after terminal scrolls down. */
	a_uint32 trow;
#define ALF_SCROLLWIN 0x01 /* last char will scroll window */
	a_ubyte flags;
};

ARRAY_NEW(a_arr_line, struct a_line)

/* terminal input */
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
 * Insert character 'c' under the cursor into the
 * input buffer and update cursor.
 *
 * If the input size limit is reached, character
 * won't get inserted and return value will be 0.
 *
 * If 'hidecur' is true, then cursor will be hidden
 * until screen is redrawn.
 */
a_ubyte ashe_insert_char(a_ubyte c, a_ubyte hidecur);

/*
 * Remove character under the cursor from the
 * input buffer and update cursor.
 *
 * In case character was not removed, meaning
 * input buffer was already empty, return value
 * will be 0.
 */
a_ubyte ashe_remove_char(void);

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
a_uint32 ashe_remove_bytes(a_ssize len); // TODO: test

/*
 * Insert new line under the cursor and move the
 * cursor to the start of that line.
 *
 * If cursor is not preceeded by the open double
 * quotes and the preceeding character is not escape
 * character then this stops reading from terminal.
 * Meaning shell will try to execute the current
 * input buffer.
 * In this case the function returns 1.
 */
a_ubyte ashe_cr(void);

/* Redraw the prompt and clear the input buffer. */
void ashe_redraw_prompt(void);

/*
 * Move terminal cursor once to the left.
 *
 * If cursor is already at the start of
 * the input buffer 0 is returned.
 */
a_ubyte ashe_move_left(void);

/*
 * Move terminal cursor once to the right.
 *
 * If cursor is already at the end of the
 * input buffer 0 is returned.
 */
a_ubyte ashe_move_right(void);

/*
 * Move terminal cursor down once.
 *
 * If cursor is already at the last
 * terminal row of the input 0 is returned.
 */
a_ubyte ashe_move_down(void);

/*
 * Move terminal cursor up once.
 *
 * If cursor is already at the first
 * terminal row of the input 0 is returned.
 */
a_ubyte ashe_move_up(void);

/*
 * Move cursor to the end of the terminal line.
 *
 * If cursor is already at the end of
 * the terminal line 0 is returned.
 */
a_ubyte ashe_move_to_eol(void);

/*
 * Move cursor to the start of the terminal line.
 *
 * If cursor is already at the start of
 * the terminal line 0 is returned.
 */
a_ubyte ashe_move_to_sol(void);

/*
 * Clears the screen, redraws the prompt together
 * with the input buffer and sets the terminal cursor
 * to the original position inside the input buffer.
 */
void ashe_clear_screen_and_redraw(void);

/*
 * Moves cursor to the start of the input buffer.
 *
 * If the cursor was already at the start this
 * returns 0.
 */
a_ubyte ashe_move_to_start(void);

/*
 * Moves cursor to the end of the input buffer.
 *
 * If the cursor was already at the start this
 * returns 0.
 */
a_ubyte ashe_move_to_end(void);


a_ubyte ashe_draw_prompt_unsafe(void);
void ashe_redraw_input_unsafe(void);
void ashe_clear_screen_unsafe(void);

#endif
