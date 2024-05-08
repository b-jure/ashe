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

#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "acommon.h"
#include "autils.h"
#include "ainput.h"
#include "ashell.h"
#include "aasync.h"
#include "auserstr.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#define ASHE_IBF_MAX ASHE_USERSTR_MAX

/* control sequence introducer */
#define A_CSI	   "\033["
#define A_ESC(seq) A_CSI #seq

/* cursor control sequences */
#define a_csi_cursor_home	    A_ESC(H)
#define a_csi_cursor_left(n)	    A_ESC(n) "D"
#define a_csi_cursor_right(n)	    A_ESC(n) "C"
#define a_csi_cursor_up(n)	    A_ESC(n) "A"
#define a_csi_cursor_down(n)	    A_ESC(n) "B"
#define a_csi_cursor_save	    A_ESC(s)
#define a_csi_cursor_load	    A_ESC(u)
#define a_csi_cursor_col(col)	    A_ESC(col) "G"
#define a_csi_cursor_move(row, col) A_ESC(row ";" col) "H"
#define a_csi_cursor_position	    A_ESC(6n)
#define a_csi_cursor_hide 	    A_ESC(?25l)
#define a_csi_cursor_show 	    A_ESC(?25h)
#define a_csi_scroll_down A_ESC(M)

/* clear control sequences */
#define a_csi_clear_down       A_ESC(0J)
#define a_csi_clear_up	       A_ESC(1J)
#define a_csi_clear_all	       A_ESC(2J)
#define a_csi_clear_line_right A_ESC(0K)
#define a_csi_clear_line_left  A_ESC(1K)
#define a_csi_clear_line       A_ESC(2K)

/* key code defs */
#define CTRL_KEY(k)    ((k) & 0x1f)
#define ESCAPE	       27
#define CR	       0x0D
#define IMPLEMENTED(c) (c != ESCAPE)

/* terminal column position */
#define tcol(x) ((((x)-1) % (A_TCOLMAX)) + 1)

/* terminal row diff */
#define trowdiff(x, y) (((x) / A_TCOLMAX) - ((y) / A_TCOLMAX))
#define trowdiffx(x)   ((x) / A_TCOLMAX)

/* draw buffer */
#define dbf_pushc(c)	     a_arr_char_push(&A_TDBF, c)
#define dbf_push(s)	     a_arr_char_push_str(&A_TDBF, s, strlen(s))
#define dbf_push_len(s, len) a_arr_char_push_str(&A_TDBF, s, len)
#define dbf_push_movecol(n)  a_arr_char_push_strf(&A_TDBF, A_CSI "%nG", n)
#define dbf_push_moveup(n)   a_arr_char_push_strf(&A_TDBF, A_CSI "%nA", n)
#define dbf_push_movedown(n) a_arr_char_push_strf(&A_TDBF, A_CSI "%nB", n)
#define dbf_pushlit(strlit)  dbf_push_len(strlit, SS(strlit))

/* draw without buffering */
#define draw_lit(strlit) ashe_write(STDERR_FILENO, strlit, SS(strlit))

/* turn on OPOST in raw mode */
#define opost_on()                                           \
	do {                                                 \
		ashe.sh_term.tm_rawtermios.c_oflag |= OPOST; \
		ashe_tcsetattr(TCSAFLUSH, &A_TIORAW);        \
	} while (0)

/* turn off OPOST in raw mode */
#define opost_off()                                             \
	do {                                                    \
		ashe.sh_term.tm_rawtermios.c_oflag &= ~(OPOST); \
		ashe_tcsetattr(TCSAFLUSH, &A_TIORAW);           \
	} while (0)

/*
 * shift lines starting from 'row', 'n' bytes
 * to right or left depending on the 'sign'
 */
#define shift_lines_from(row, n, sign)                \
	for (a_uint32 i = row; i < A_ILINES.len; i++) \
		a_arr_line_index(&A_ILINES, i)->start sign## = (n);

/* Implemented keys */
enum termkey {
	BACKSPACE = 127,
	L_ARW = 1000,
	U_ARW,
	D_ARW,
	R_ARW,
	HOME_KEY,
	END_KEY,
	DEL_KEY,
};

ASHE_PRIVATE inline void dbf_flush()
{
	opost_on();
	ashe_write(STDERR_FILENO, a_arr_ptr(A_TDBF), a_arr_len(A_TDBF));
	opost_off();
	a_arr_len(A_TDBF) = 0;
}

ASHE_PRIVATE void get_winsize_fallback(void)
{
	a_uint32 oldrow, oldcol;

	draw_lit(a_csi_cursor_hide a_csi_cursor_save a_csi_cursor_right(99999)
			 a_csi_cursor_down(99999));
	oldrow = A_TROW;
	oldcol = A_TCOL;
	a_term_sync_cursor();
	A_TROWMAX = A_TROW;
	A_TCOLMAX = A_TCOL;
	A_TROW = oldrow;
	A_TCOL = oldcol;
	draw_lit(a_csi_cursor_load a_csi_cursor_show);
}

/* Auxiliary to 'a_term_init()' */
ASHE_PRIVATE void init_rawterm(struct termios *rawterm)
{
	ashe_tcgetattr(rawterm);
	rawterm->c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
	rawterm->c_oflag &= ~(OPOST);
	rawterm->c_lflag &= ~(ECHO | ICANON | IEXTEN);
	rawterm->c_cflag |= (CS8);
	rawterm->c_cc[VMIN] = 1;
	rawterm->c_cc[VTIME] = 0;
}

ASHE_PRIVATE void a_input_init()
{
	a_arr_char_init_cap(&A_IBF, 8);
	A_IBFIDX = 0;
	/* input lines */
	a_arr_line_init(&A_ILINES);
	a_arr_line_push(&A_ILINES, (struct a_line){ .len = 0, .start = a_arr_ptr(A_IBF) });
	A_ICOL = 0;
	A_IROW = 0;
	/* rest is set dynamically */
}

ASHE_PRIVATE void a_input_free(void)
{
	a_arr_char_free(&A_IBF, NULL);
	a_arr_line_free(&A_ILINES, NULL);
}

/*
 * Re-links the slices (lines) to the input buffer.
 * This should be called after inserting bytes into
 * the input buffer because buffer might reallocate
 * to a different memory address.
 */
ASHE_PRIVATE void relink_lines(void)
{
	struct a_line *prev;
	struct a_line *curr;
	a_uint32 i, lines;

	lines = a_arr_len(A_ILINES);
	prev = a_arr_line_index(&A_ILINES, 0);
	prev->start = a_arr_ptr(A_IBF);

	for (i = 1; i < lines; i++) {
		curr = a_arr_line_index(&A_ILINES, i);
		curr->start = prev->start + prev->len;
		prev = curr;
	}
}

/* Redraw prompt, do not update cursor. */
ASHE_PUBLIC void ashe_redraw_prompt_unsafe(void)
{
	dbf_pushlit(a_csi_cursor_hide);
	dbf_push_len(a_arr_ptr(A_TP), A_TPLEN);
	dbf_pushlit(a_csi_cursor_show);
	dbf_flush();
}

/*
 * Return number of rows between the current row
 * and the first row where the prompt resides
 * if the 'prompt' is non zero value.
 * Otherwise return the number of rows between the
 * current row and the first terminal row where the
 * input buffer begins.
 */
ASHE_PRIVATE a_uint32 first_row_diff(a_ubyte prompt)
{
	struct a_line *line;
	a_ssize temp;
	a_uint32 idx, rows;

	prompt = (prompt != 0);
	idx = A_IROW;
	line = a_arr_line_index(&A_ILINES, idx);
	temp = line->len;

	for (rows = 0; 0 < idx; idx--) {
		rows += trowdiffx(temp - 1) + 1;
		line = a_arr_line_index(&A_ILINES, idx);
		temp = line->len;
	}

	rows += trowdiffx(temp - 1 + (prompt * A_TPLEN));

	return rows;
}

/*
 * Return number of rows between the current row
 * and the last input buffer row.
 */
ASHE_PRIVATE a_uint32 last_row_diff(void)
{
	a_ssize temp;
	a_uint32 idx, rows, lines, extra;

	lines = a_arr_len(A_ILINES);
	idx = A_IROW;
	extra = (idx == 0) * A_TPLEN;
	temp = a_arr_line_index(&A_ILINES, idx)->len;
	temp += extra;
	temp -= trowdiffx(A_ICOL + extra);

	for (rows = 0; idx < lines; idx++) {
		rows += trowdiffx(temp - 1) + 1;
		temp = a_arr_line_index(&A_ILINES, idx)->len;
	}
	if (idx != 0)
		rows--; /* we overshoot by one row always */

	return rows;
}

ASHE_PRIVATE enum termkey read_key(void)
{
	a_ubyte seq[3];
	a_int32 nread;
	a_byte c;

	ashe_mask_signals(SIG_UNBLOCK);
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (ASHE_UNLIKELY(nread == -1 && (errno != EINTR && !ashe.sh_int)))
			ashe_panic_libcall(read);
		ashe.sh_int = 0;
	}
	ashe_mask_signals(SIG_BLOCK);

	if (c == ESCAPE) {
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return ESCAPE;
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return ESCAPE;
		if (seq[0] == '[') {
			if (isdigit(seq[1])) {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return ESCAPE;
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '3':
						return DEL_KEY;
					case '1':
					case '7':
						return HOME_KEY;
					case '4':
					case '8':
						return END_KEY;
					default:
						break;
					}
				}
			} else {
				switch (seq[1]) {
				case 'C':
					return R_ARW;
				case 'D':
					return L_ARW;
				case 'B':
					return D_ARW;
				case 'A':
					return U_ARW;
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				default:
					break;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
			case 'H':
				return HOME_KEY;
			case 'F':
				return HOME_KEY;
			default:
				break;
			}
		}
		return ESCAPE;
	} else {
		return c;
	}
}

ASHE_PRIVATE a_ubyte process_key(void)
{
	a_int32 c;

	if (IMPLEMENTED((c = read_key()))) {
		switch (c) {
		case CR:
			if (ashe_cr() == 0)
				break;
			return 0;
		case DEL_KEY:
		case BACKSPACE:
			ashe_remove_char();
			break;
		case END_KEY:
			ashe_move_to_eol();
			break;
		case HOME_KEY:
			ashe_move_to_sol();
			break;
		case CTRL_KEY('l'):
			ashe_clear_screen_and_redraw();
			break;
		case L_ARW:
			ashe_move_left();
			break;
		case R_ARW:
			ashe_move_right();
			break;
		case U_ARW:
			ashe_move_up();
			break;
		case D_ARW:
			ashe_move_down();
			break;
		case CTRL_KEY('h'):
		case CTRL_KEY('x'):
		case CTRL_KEY('j'):
		case CTRL_KEY('k'):
		case CTRL_KEY('i'):
			break;
		default:
			if (isgraph(c) || isspace(c))
				ashe_insert_char(c);
			break;
		}
	}
#ifdef ASHE_DBG_CURSOR
	debug_cursor();
#endif
#ifdef ASHE_DBG_LINES
	debug_lines();
#endif
	return 1;
}

ASHE_PRIVATE void a_input_read(void)
{
	a_term_sync_cursor(); /* sync once in raw mode */
#ifdef ASHE_DBG_CURSOR
	debug_cursor();
#endif
#ifdef ASHE_DBG_LINES
	debug_lines();
#endif
	while (process_key())
		;
	a_arr_char_push(&A_IBF, '\0');
	ashe_move_to_end();
}

ASHE_PUBLIC void a_input_clear(void)
{
	a_input_free();
	a_input_init();
}

ASHE_PUBLIC void a_term_init(void)
{
	a_arr_char_init_cap(&A_TP, sizeof(ASHE_PROMPT));
	a_input_init();
	a_arr_char_init_cap(&A_TDBF, 8);
	ashe_tcgetattr(&A_TIODFL); /* init default termios */
	init_rawterm(&A_TIORAW); /* init raw mode */
	a_term_sync_dimensions();
	/* tm_col - gets set when reading and drawing */
	A_TM.tm_reading = 0;
}

ASHE_PUBLIC void a_term_read(void)
{
	ashe_mask_signals(SIG_BLOCK);
	ashe_dprint("[R]EPL");
	a_input_clear();
	ashe_draw_prompt_unsafe();
	A_TM.tm_reading = 1;
	ashe_tcsetattr(TCSAFLUSH, &A_TIORAW);
	a_input_read();
	ashe_tcsetattr(TCSAFLUSH, &A_TIODFL);
	A_TM.tm_reading = 0;
	ashe_print("\n", stderr);
}

ASHE_PUBLIC void a_term_sync_cursor(void)
{
	char buf[ASHE_MAXINT16STR * 2 + sizeof(A_CSI ";")]; /* A_ESC [ Pn ; Pn R */
	a_ssize nread;
	a_uint32 srow, scol;
	a_uint16 i;
	char c;

	i = 0;
	draw_lit(a_csi_cursor_position);
	while (i < sizeof(buf) && (nread = read(STDIN_FILENO, &c, 1)) == 1) {
		if (c == 'R')
			break;
		buf[i++] = c;
	}

	if (ASHE_UNLIKELY(sscanf(buf, "\033[%u;%u", &srow, &scol) != 2))
		ashe_panic_libcall(sscanf);

	A_TROW = srow;
	A_TCOL = scol;
}

ASHE_PUBLIC void a_term_sync_dimensions(void)
{
	struct winsize ws;

	if (ASHE_UNLIKELY(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0)) {
		get_winsize_fallback();
		return;
	}
	A_TROWMAX = ws.ws_row;
	A_TCOLMAX = ws.ws_col;
}

ASHE_PUBLIC void a_term_free(void)
{
	a_arr_char_free(&A_TP, NULL);
	a_input_free();
	a_arr_char_free(&A_TDBF, NULL);
}

ASHE_PUBLIC a_ubyte ashe_insert_char(a_ubyte c)
{
	struct a_line newline;
	a_uint32 idx;
	a_ubyte relink;

	/* return if input limit would be exceeded */
	if (ASHE_UNLIKELY(a_arr_len(A_IBF) >= ARG_MAX - 1))
		return 0;

	/* update state and input buffer */
	idx = A_IBFIDX;
	relink = (a_arr_len(A_IBF) >= a_arr_cap(A_IBF));
	a_arr_char_insert(&A_IBF, A_IBFIDX, c);
	A_ILINE.len++;
	if (ASHE_UNLIKELY(relink)) {
		relink_lines();
		relink = 0;
	}

	/* draw */
	shift_lines_from(A_IROW + 1, 1, +); /* shift right */
	dbf_pushlit(a_csi_cursor_hide a_csi_clear_line_right a_csi_clear_down);
	if (c == '\n' && A_TCOL < A_TCOLMAX) { /* inside of 'ashe_i_cr()' ? */
		dbf_pushc('\n');
		idx++;
	}
	dbf_pushlit(a_csi_cursor_save);
	dbf_push_len(a_arr_char_index(&A_IBF, idx), a_arr_len(A_IBF) - idx);
	dbf_pushlit(a_csi_cursor_load a_csi_cursor_show);
	dbf_flush();

	if (c == '\n') {
		A_TROW++;

		newline.start = A_ILINE.start + A_ICOL + 1;
		newline.len = A_ILINE.len - (A_ICOL + 1);
		newline.trow = ++A_TROW;
		newline.flags = 0;
		a_arr_line_insert(&A_ILINES, ++A_IROW, newline);

		A_ILINE.len = A_ICOL + 1;
		A_IBFIDX++;
		A_ICOL = 0;
		A_TCOL = 1;
		return 1;
	}

	ashe_move_right();
	if (A_TROW > A_TROWMAX) {
		ashe_assert(A_IBFIDX == A_IBF.len);
		A_TROW--;
		draw_lit("\n");
	}

	/* TODO: fix input that is located in scroll area
	lastline = a_arr_line_last(&A_ILINES);
	lasttrow = lastline->trow + trowdiffx(lastline->len - 1);
	if (lasttrow == A_TROWMAX) {
		if (A_TROW > A_TROWMAX) {
			relink = 1;
			A_TROW--;
			if (c != '\n' && A_IBFIDX == A_IBF.len)
				draw_lit("\n");
			else
				draw_lit(a_csi_cursor_up(1));
		} else if (tcol(A_ILINE.len == 1)) {
			relink = 1;
			draw_lit(a_csi_cursor_up(1));
		}
	}
	*/

	return 1;
}

ASHE_PUBLIC a_ubyte ashe_remove_char(void)
{
	struct a_line *l;
	a_ubyte coalesce;

	if (A_IBFIDX <= 0)
		return 0;
	a_arr_char_remove(&A_IBF, A_IBFIDX - 1);
	shift_lines_from(A_IROW + 1, 1, -); /* shift left */
	A_ILINE.len -= !(coalesce = (A_ICOL == 0));
	l = &A_ILINE; /* cache current line */
	ashe_move_left();
	if (coalesce) {
		ashe_assert(A_ILINES.len > 1);
		A_ILINE.len--; /* '\n' */
		A_ILINE.len += l->len;
		a_arr_line_remove(&A_ILINES, A_IROW + 1);
		A_TROW--;
	}
	dbf_pushlit(a_csi_cursor_hide a_csi_clear_line_right a_csi_clear_down a_csi_cursor_save);
	dbf_push_len(a_arr_char_index(&A_IBF, A_IBFIDX), a_arr_len(A_IBF) - A_IBFIDX);
	dbf_pushlit(a_csi_cursor_load a_csi_cursor_show);
	dbf_flush();
	return 1;
}

/*
 * TODO: needs testing
 */
ASHE_PUBLIC a_uint32 ashe_remove_bytes(a_ssize len)
{
	struct a_line *line;
	a_uint32 bufflen, toremove, rmlines, i;
	a_ssize leftover;

	bufflen = a_arr_len(A_IBF);
	leftover = bufflen - A_IBFIDX;
	if (bufflen == 0 || len > leftover)
		return 0;
	if (len < 0 || len == leftover) {
		toremove = leftover;
		a_arr_len(A_ILINES) = A_IROW + 1;
		a_arr_len(A_IBF) = A_IBFIDX + 1;
		A_ILINE.len = A_ICOL;
		return toremove;
	}

	toremove = len;
	leftover = toremove - (A_ILINE.len - A_ICOL);
	A_ILINE.len = A_ICOL;
	rmlines = 0;

	for (i = A_IROW + 1; i < a_arr_len(A_ILINES); i++) {
		line = a_arr_line_index(&A_ILINES, i);
		if ((leftover -= line->len) <= 0) {
			leftover += line->len;
			break;
		}
		rmlines++;
	}

	if (rmlines > 0)
		a_arr_line_remove_n(&A_ILINES, A_IROW + 1, rmlines);

	if (leftover >= 0 && A_IROW < a_arr_len(A_ILINES)) { /* coalesce ? */
		line = a_arr_line_index(&A_ILINES, A_IROW + 1);
		A_ILINE.len += line->len;
		A_ILINE.len -= leftover;
		a_arr_line_remove(&A_ILINES, A_IROW + 1);
		shift_lines_from(A_IROW + 1, toremove, -);
	}

	a_arr_char_remove_n(&A_IBF, A_IBFIDX, toremove);

	/* reflect changes on the terminal screen */
	dbf_pushlit(a_csi_cursor_hide a_csi_clear_line_right a_csi_clear_down a_csi_cursor_save);
	dbf_push_len(a_arr_char_index(&A_IBF, A_IBFIDX), a_arr_len(A_IBF) - A_IBFIDX);
	dbf_pushlit(a_csi_cursor_load a_csi_cursor_show);
	dbf_flush();
	return 1;
}

ASHE_PUBLIC a_ubyte ashe_cr(void)
{
	if (ashe_isescaped(a_arr_ptr(A_IBF), A_IBFIDX) || ashe_indq(a_arr_ptr(A_IBF), A_IBFIDX))
		ashe_insert_char('\n');
	return 1;
}

/*
 * Prompt must not have newline or else it
 * will mess up cursor navigation.
 * Additionally tabs are removed also because
 * the shell doesn't support tabs (yet).
 *
 * TODO: add support for tabs by adding separate array
 * of graphical bytes for each line.
 */
ASHE_PRIVATE void sanitize_prompt(void)
{
	char *p = a_arr_ptr(A_TP);
	a_ubyte c;

	while ((c = *p)) {
		switch (c) {
		case '\n':
		case '\r':
		case '\t':
		case '\v':
		case '\f':
			*p = ' ';
			break;
		default:
			break;
		}
		p++;
	}
}

ASHE_PUBLIC a_ubyte ashe_draw_prompt_unsafe(void)
{
	a_arr_len(A_TP) = 0;
	parse_placeholders(&A_TP, ASHE_PROMPT);
	sanitize_prompt();
	if (ASHE_UNLIKELY(a_arr_len(A_TP) >= ASHE_USERSTR_MAX)) {
		a_arr_len(A_TP) = ASHE_USERSTR_MAX - 1;
		a_arr_char_push(&A_TP, '\0');
	}
	ashe_print(a_arr_ptr(A_TP), stderr);
	return 1;
}

ASHE_PUBLIC void ashe_redraw_prompt(void)
{
	ashe_move_to_end();
	a_input_clear();
	ashe_print("\r\n", stderr);
	ashe_draw_prompt_unsafe();
	a_term_sync_cursor();
}

ASHE_PUBLIC a_ubyte ashe_move_left(void)
{
	if (A_ICOL > 0) {
		A_ICOL--;
		if (A_TCOL == 1) {
			A_TCOL = A_TCOLMAX;
			goto lineuptocol;
		} else {
			A_TCOL--;
			draw_lit(a_csi_cursor_left(1));
		}
	} else if (A_IROW > 0) {
		A_IROW--;
		A_ICOL = A_ILINE.len - 1;
		A_TCOL = tcol(A_ILINE.len + ((A_IROW == 0) * A_TPLEN));
lineuptocol:
		A_TROW--;
		dbf_pushlit(a_csi_cursor_up(1));
		dbf_push_movecol(A_TCOL);
		dbf_flush();
	} else {
		return 0;
	}
	A_IBFIDX--;
	return 1;
}

ASHE_PUBLIC a_ubyte ashe_move_right(void)
{
	if (A_ICOL < A_ILINE.len - (A_IROW < A_ILINES.len - 1)) {
		A_ICOL++;
		if (A_TCOL < A_TCOLMAX) {
			A_TCOL++;
			draw_lit(a_csi_cursor_right(1));
		} else {
			goto firstcoldown;
		}
	} else if (A_IROW < (A_ILINES.len - 1)) {
		A_IROW++;
		A_ICOL = 0;
firstcoldown:
		A_TROW++;
		A_TCOL = 1;
		draw_lit(a_csi_cursor_down(1) a_csi_cursor_col(1));
	} else {
		return 0;
	}
	A_IBFIDX++;
	return 1;
}

ASHE_PUBLIC a_ubyte ashe_move_down(void)
{
	ashe_assert(A_ILINES.len > 0);
	a_uint32 extra = (A_IROW == 0) * A_TPLEN;
	a_ubyte notlastrow = (A_IROW < A_ILINES.len - 1);
	a_uint32 linewraps = (A_ILINE.len != 0) * trowdiffx(A_ILINE.len + extra - notlastrow);
	a_uint32 colwraps = (A_ICOL != 0) * trowdiffx(A_ICOL + extra);
	a_ssize wraps = linewraps - colwraps;
	a_ubyte temp;

	if (wraps > 0) {
		if (wraps > 1 || A_ICOL + extra + A_TCOLMAX <= A_ILINE.len + extra - 1) {
			A_ICOL += A_TCOLMAX;
			A_IBFIDX += A_TCOLMAX;
			goto down;
		} else {
			temp = (A_IROW < A_ILINES.len - 1);
			A_IBFIDX += A_ILINE.len - A_ICOL - temp;
			A_ICOL = A_ILINE.len - temp;
			A_TCOL = tcol(A_ILINE.len + extra + !temp);
			goto downtocol;
		}
	} else if (A_IROW < A_ILINES.len - 1) {
		A_IBFIDX += A_ILINE.len - A_ICOL; /* start of new line */
		A_IROW++;
		if (A_ILINE.len >= A_TCOLMAX || A_ILINE.len >= tcol(A_ICOL + 1 + extra)) {
			A_IBFIDX += A_TCOL - 1;
			A_ICOL = A_TCOL - 1;
down:
			dbf_pushlit(a_csi_cursor_down(1));
			goto flush;
		} else {
			temp = (A_IROW != A_ILINES.len - 1);
			A_IBFIDX += A_ILINE.len - temp;
			A_ICOL = A_ILINE.len - temp;
			A_TCOL = A_ILINE.len + !temp;
downtocol:
			dbf_pushlit(a_csi_cursor_down(1));
			dbf_push_movecol(A_TCOL);
flush:
			dbf_flush();
		}
		A_TROW++;
		return 1;
	}
	return 0;
}

ASHE_PUBLIC a_ubyte ashe_move_up(void)
{
	a_uint32 extra = ((A_IROW == 0) * A_TPLEN);
	a_uint32 len = A_ICOL + 1 + extra;
	a_uint32 pwraps = (A_TPLEN != 0) * ((A_TPLEN - 1) / A_TCOLMAX);
	a_uint32 lwraps = (len - 1) / A_TCOLMAX;
	a_uint32 temp;

	temp = lwraps - pwraps;

	if (A_IROW == 0 && (temp == 0 || (temp < 2 && len % A_TCOLMAX == 0)))
		return 0;

	if (A_IROW == 0) {
		if (temp == 1 && (temp = tcol(A_TPLEN)) >= tcol(len)) {
start:
			A_ICOL = 0;
			A_IBFIDX = 0;
			A_TCOL = temp + 1;
			goto uptocol_draw;
		} else {
			goto up;
		}
	} else if (lwraps == 0) {
		A_IBFIDX -= A_ICOL + 1; /* end of the line above */
		A_IROW--;
		extra = (A_IROW == 0) * A_TPLEN;
		len = A_ILINE.len + extra;
		lwraps = (len - 1) / A_TCOLMAX;
		if ((temp = tcol(len)) <= A_ICOL + 1) {
			A_ICOL = A_ILINE.len - 1;
			A_TCOL = temp;
			goto uptocol_draw;
		} else {
			temp = tcol(A_ICOL + 1);
			if (A_IROW == 0 && lwraps - pwraps == 0 && tcol(A_TPLEN) >= temp) {
				temp = tcol(A_TPLEN + 1);
				goto start;
			}
			temp = tcol(len) - temp;
			A_ICOL = A_ILINE.len - temp - 1;
			A_IBFIDX -= temp;
			goto up_draw;
		}
	} else { /* lwraps > 0 */
up:
		A_ICOL -= A_TCOLMAX;
		A_IBFIDX -= A_TCOLMAX;
up_draw:
		dbf_pushlit(a_csi_cursor_up(1));
		goto flush;
	}
uptocol_draw:
	dbf_pushlit(a_csi_cursor_up(1));
	dbf_push_movecol(A_TCOL);
flush:
	dbf_flush();
	A_TROW--;
	return 1;
}

ASHE_PUBLIC a_ubyte ashe_move_to_eol(void)
{
	a_uint32 extra = (A_IROW == 0) * A_TPLEN;
	a_uint32 col = A_ICOL + extra;
	a_uint32 len = A_ILINE.len + extra;
	a_uint32 temp;
	a_ubyte isnotlastrow = !(A_ILINES.len == 1 || A_IROW == A_ILINES.len - 1);

	if (A_ILINE.len > 0 &&
	    ((temp = tcol(col + 1)) != A_TCOLMAX || A_ICOL != A_ILINE.len - isnotlastrow)) {
		ashe_assert(A_ILINE.len != 0);
		if (trowdiffx(col) < trowdiffx(len)) {
			A_TCOL = A_TCOLMAX;
			A_ICOL += A_TCOLMAX - temp;
			A_IBFIDX += A_TCOLMAX - temp;
		} else {
			A_TCOL = tcol(len) + !isnotlastrow;
			A_IBFIDX += A_ILINE.len - A_ICOL - isnotlastrow;
			A_ICOL = A_ILINE.len - isnotlastrow;
		}
		dbf_push_movecol(A_TCOL);
		dbf_flush();
		return 1;
	}
	return 0;
}

ASHE_PUBLIC a_ubyte ashe_move_to_sol(void)
{
	a_uint32 extra = (A_IROW == 0) * A_TPLEN;
	a_uint32 col = A_ICOL + extra;
	a_ssize temp;

	if (A_ICOL != 0 && (temp = tcol(col + 1)) != 1) {
		if (col < A_TCOLMAX) {
			A_IBFIDX -= A_ICOL;
			A_ICOL = 0;
			A_TCOL = extra + 1;
		} else {
			A_IBFIDX -= temp - 1;
			A_ICOL -= temp - 1;
			col = A_ICOL + extra;
			A_TCOL = tcol(col + 1);
		}
		dbf_push_movecol(A_TCOL);
		dbf_flush();
		return 1;
	}
	return 0;
}

ASHE_PUBLIC void ashe_clear_screen_unsafe(void)
{
	draw_lit(a_csi_cursor_hide a_csi_cursor_home a_csi_clear_all a_csi_cursor_show);
}

ASHE_PUBLIC void ashe_clear_screen_and_redraw(void)
{
	ashe_clear_screen_unsafe();
	ashe_draw_prompt_unsafe();
	ashe_redraw_input_unsafe();
	a_term_sync_cursor();
}

/* Only draws to the terminal screen. */
ASHE_PUBLIC void ashe_redraw_input_unsafe(void)
{
	dbf_pushlit(a_csi_cursor_hide);
	dbf_push_len(a_arr_ptr(A_IBF), A_IBFIDX);
	dbf_pushlit(a_csi_cursor_save);
	dbf_push_len(a_arr_ptr(A_IBF) + A_IBFIDX, a_arr_len(A_IBF) - A_IBFIDX);
	dbf_pushlit(a_csi_cursor_load a_csi_cursor_show);
	dbf_flush();
}

ASHE_PUBLIC a_ubyte ashe_move_to_start(void)
{
	a_int32 up;

	if (A_IBFIDX == 0) /* already at start ? */
		return 0;

	/* update input buffer */
	up = first_row_diff(0);
	A_IROW -= up;
	A_TCOL = tcol(A_TPLEN + 1);
	A_IBFIDX = 0;
	A_ICOL = 0;
	A_IROW = 0;

	/* update terminal cursor */
	dbf_pushlit(a_csi_cursor_hide);
	dbf_push_moveup(up);
	dbf_push_movecol(A_TCOL);
	dbf_pushlit(a_csi_cursor_show);
	dbf_flush();

	return 1;
}

ASHE_PUBLIC a_ubyte ashe_move_to_end(void)
{
	struct a_line *line;
	a_uint32 lines, down;

	if (A_IBFIDX == a_arr_len(A_IBF)) /* already at end ? */
		return 0;

	/* update input buffer */
	down = last_row_diff();
	lines = a_arr_len(A_ILINES);
	line = a_arr_line_last(&A_ILINES);
	A_IBFIDX = a_arr_len(A_IBF);
	A_IROW = lines - (lines == 0);
	A_ICOL = line->len + (lines == 0) * A_TPLEN;
	A_IROW += down;
	A_TCOL = tcol(A_ICOL);

	/* update terminal cursor */
	dbf_pushlit(a_csi_cursor_hide);
	dbf_push_movedown(down);
	dbf_push_movecol(tcol(A_ICOL));
	dbf_pushlit(a_csi_cursor_show);
	dbf_flush();

	return 1;
}

/*
 * *Hopefully* redraws the prompt with the input
 * properly each time terminal window resizes.
 */
ASHE_PUBLIC void sigwinch_redraw(void)
{
	a_int32 up;

	up = first_row_diff(1);
	a_term_sync_dimensions();
	up -= A_TCOLMAX == 1;
	dbf_pushlit(a_csi_cursor_hide);
	if (up > 0)
		dbf_push_moveup(up);
	dbf_pushlit(a_csi_cursor_col(1) a_csi_clear_line_right a_csi_clear_down);
	dbf_push_len(a_arr_ptr(A_TP), A_TPLEN);
	dbf_push_len(a_arr_ptr(A_IBF), A_IBFIDX);
	dbf_pushlit(a_csi_cursor_save);
	dbf_push_len(a_arr_char_index(&A_IBF, A_IBFIDX), a_arr_len(A_IBF) - A_IBFIDX);
	dbf_pushlit(a_csi_cursor_load a_csi_cursor_show);
	dbf_flush();
	a_term_sync_cursor();
}
