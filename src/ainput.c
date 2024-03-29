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
#include "aprompt.h"
#ifdef ASHE_DBG
#include "adbg.h"
#endif

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESCAPE	    27
#define CR	    0x0D

#define IMPLEMENTED(c) (c != ESCAPE)
#define modlen(x, y)   ((((x)-1) % (y)) + 1)

#define dbf_draw_lit(strlit) write_or_panic(strlit, sizeof(strlit) - 1)
#define dbf_pushlit(strlit)  dbf_push_len(strlit, sizeof(strlit) - 1)
#define dbf_push_movecol(n)       \
	do {                      \
		dbf_pushlit(CSI); \
		dbf_push_unum(n); \
		dbf_pushc('G');   \
	} while (0)

#define WRAPS(len) ((len) >= TCOLMAX)

#define WHILE_READING(ti) while (TerminalInput_process_key(ti))

/* Draw buffer */
#define dbf_pushc(c)	     Buffer_push(&DBF, c)
#define dbf_push(s)	     Buffer_push_str(&DBF, s, strlen(s))
#define dbf_push_len(s, len) Buffer_push_str(&DBF, s, len)

enum termkey {
	BACKSPACE = 127,
	L_ARW = 1000,
	U_ARW,
	D_ARW,
	R_ARW,
	HOME_KEY,
	END_KEY,
	DEL_KEY
};

ASHE_PRIVATE inline void set_terminal_mode(struct termios *tmode)
{
	if (unlikely(tcsetattr(STDIN_FILENO, TCSAFLUSH, tmode) < 0))
		panic(NULL);
}

ASHE_PRIVATE inline void turn_on_nltrans(void)
{
	ashe.sh_term.tm_rawtermios.c_oflag |= OPOST;
	set_terminal_mode(&ashe.sh_term.tm_rawtermios);
}

ASHE_PRIVATE inline void turn_off_nltrans(void)
{
	ashe.sh_term.tm_rawtermios.c_oflag &= ~(OPOST);
	set_terminal_mode(&ashe.sh_term.tm_rawtermios);
}

ASHE_PRIVATE inline void dbf_push_unum(memmax n)
{
	char temp[UINT_DIGITS];
	int32 chars;

	if (unlikely((chars = snprintf(temp, UINT_DIGITS, "%zu", n)) < 0)) {
		print_errno();
		panic(NULL);
	}
	Buffer_push_str(&DBF, temp, chars);
}

ASHE_PRIVATE inline void dbf_flush()
{
	Buffer *drawbuf = &DBF;
	turn_on_nltrans();
	write_or_panic(drawbuf->data, drawbuf->len);
	turn_off_nltrans();
	drawbuf->len = 0;
}

ASHE_PRIVATE int32 get_window_size_fallback(uint32 *height, uint32 *width)
{
	int32 res;

	dbf_draw_lit(cursor_save cursor_right(99999) cursor_down(99999));
	res = get_cursor_pos(height, width);
	dbf_draw_lit(cursor_load);
	return res;
}

ASHE_PUBLIC int32 get_window_size(uint32 *height, uint32 *width)
{
	struct winsize ws;

	if (unlikely(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 ||
		     ws.ws_col == 0)) {
		return get_window_size_fallback(height, width);
	} else {
		if (height)
			*height = ws.ws_row;
		if (width)
			*width = ws.ws_col;
		return 0;
	}
}

ASHE_PUBLIC int32 get_cursor_pos(uint32 *row, uint32 *col)
{
	char c;
	memmax i = 0;
	int32 nread;
	uint32 srow, scol;
	char buf[INT_DIGITS * 2 + sizeof(CSI ";")]; /* ESC [ Pn ; Pn R */

	dbf_draw_lit(cursor_position);
	while ((nread = read(STDIN_FILENO, &c, 1)) == 1) {
		if (c == 'R')
			break;
		buf[i++] = c;
	}
	if (nread == -1 || sscanf(buf, "\033[%u;%u", &srow, &scol) != 2) {
		print_errno();
		return -1;
	}
	if (row)
		*row = srow;
	if (col)
		*col = scol;
	return 0;
}

/* Auxiliary to 'Terminal_init()' */
ASHE_PRIVATE void init_rawterm(struct termios *rawterm)
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
ASHE_PRIVATE inline void init_dflterm(struct termios *dflterm)
{
	tcgetattr(STDIN_FILENO, dflterm);
}

ASHE_PUBLIC void TerminalInput_init(TerminalInput *tinput)
{
	unused(tinput);
	Buffer_init_cap(&IBF, 8);
	Buffer_init_cap(&IBF, 8);
	ArrayLine_init(&LINES);
	ArrayLine_push(&LINES, (Line){ .len = 0, .start = IBF.data });
	IBFIDX = 0;
	COL = 0;
	ROW = 0;
}

ASHE_PUBLIC void TerminalInput_free(TerminalInput *tinput)
{
	unused(tinput);
	Buffer_free(&IBF, NULL);
	Buffer_free(&DBF, NULL);
	ArrayLine_free(&LINES, NULL);
}

ASHE_PUBLIC void Terminal_init(Terminal *term)
{
	TerminalInput_init(&term->tm_input);
	init_rawterm(&term->tm_rawtermios);
	init_dflterm(&term->tm_dfltermios);
	get_winsize_or_panic(&term->tm_rows, &term->tm_columns);
	/* tm_col - gets set when reading and drawing */
	/* tm_promptlen - gets set in 'print_prompt()' */
	term->tm_reading = 0;
}

ASHE_PUBLIC void Terminal_free(Terminal *term)
{
	TerminalInput_free(&term->tm_input);
}

ASHE_PRIVATE inline void shift_lines_right(memmax start)
{
	memmax i;

	for (i = start; i < LINES.len; i++)
		LINES.data[i].start++;
}

ASHE_PRIVATE inline void shift_lines_left(memmax start)
{
	memmax i;

	for (i = start; i < LINES.len; i++)
		LINES.data[i].start--;
}

ASHE_PRIVATE ubyte TerminalInput_cursor_up(TerminalInput *tinput)
{
	unused(tinput);
	ssize temp, temp2;
	uint32 len = COL + 1 + ((ROW == 0) * PLEN);
	uint32 promptrows = PLEN / TCOLMAX;
	uint32 linerows = len / TCOLMAX;

	if (ROW == 0 && (linerows - promptrows == 0 ||
			 (linerows - promptrows < 2 && len % TCOLMAX == 0)))
		return 0;

	if (ROW == 0) {
		if (linerows - promptrows == 1 &&
		    (temp = PLEN % TCOLMAX) >= len % TCOLMAX) {
			COL = 0;
			IBFIDX = 0;
			TCOL = temp + 1;
			goto cursuptocol_draw;
		} else {
			goto cursup;
		}
	} else if (linerows == 0) {
		ROW--;
		if (ROW == 0) {
			len = LINE.len + PLEN;
			linerows = len / TCOLMAX;
			if ((temp = linerows - promptrows) > 1) {
				goto cursup;
			} else if ((temp == 1 &&
				    (temp = modlen(len, TCOLMAX)) != 0) ||
				   PLEN % TCOLMAX == 0) {
				if (temp <= COL + 1) {
					IBFIDX -= COL + 1;
					COL = LINE.len - 1;
					TCOL = temp;
					goto cursuptocol_draw;
				} else {
					goto inbetween;
				}
			} else if (COL + 1 >= (temp = (modlen(len, TCOLMAX)))) {
				IBFIDX -= COL;
				COL = LINE.len;
				TCOL = temp;
				goto cursuptocol_draw;
			} else if ((temp = modlen(PLEN, TCOLMAX)) >= COL + 1) {
				IBFIDX = 0;
				COL = 0;
				TCOL = temp + 1;
			} else {
				goto inbetween;
			}
		} else {
			if (COL + 1 >= modlen(LINE.len, TCOLMAX)) {
				IBFIDX -= COL;
				COL = LINE.len;
				TCOL = modlen(LINE.len, TCOLMAX);
				goto cursuptocol_draw;
			} else {
inbetween:
				IBFIDX -= COL + 1;
				temp = modlen(LINE.len, TCOLMAX);
				temp2 = modlen(COL + 1, TCOLMAX);
				COL = LINE.len - 1 - labs(temp - temp2);
				IBFIDX -= LINE.len - COL - 1;
				TCOL = modlen(COL + 1, TCOLMAX);
				goto cursuptocol_draw;
			}
		}
	} else { /* linerows > 0 */
cursup:
		COL -= TCOLMAX;
		IBFIDX -= TCOLMAX;
		goto cursup_draw;
	}

cursuptocol_draw:
	dbf_pushlit(cursor_up(1));
	dbf_push_movecol(TCOL);
	goto flush;
cursup_draw:
	dbf_pushlit(cursor_up(1));
flush:
	dbf_flush();
	return 1;
}

// clang-format off
ASHE_PRIVATE ubyte TerminalInput_cursor_down(TerminalInput *tinput)
{
	unused(tinput);
	uint32 extra = (ROW == 0) * PLEN;
	uint32 linewraps = (LINE.len != 0) * ((LINE.len - 1 + extra) / TCOLMAX);
	uint32 colwraps = (COL != 0) *  ((COL + extra) / TCOLMAX);
	ssize wrapdepth = linewraps - colwraps;

	if (wrapdepth > 0) {
		if (wrapdepth > 1 || COL + TCOLMAX <= LINE.len - 1) {
			COL += TCOLMAX;
			IBFIDX += TCOLMAX;
			goto down;
		} else {
			COL = LINE.len - 1;
			IBFIDX += LINE.len - 1 - COL;
			TCOL = modlen(LINE.len + extra, TCOLMAX);
			goto downtocol;
		}
	} else if (ROW < LINES.len - 1) {
		IBFIDX += LINE.len - COL - 1;
		ROW++;
		if (WRAPS(LINE.len) || LINE.len >= modlen(COL + 1 + extra, TCOLMAX)) {
			IBFIDX += TCOL;
			COL = TCOL - 1;
down:
			dbf_pushlit(cursor_down(1));
			goto flush;
		} else {
			IBFIDX += LINE.len;
			TCOL = LINE.len;
downtocol:
			dbf_pushlit(cursor_down(1));
			dbf_push_movecol(TCOL);
flush:
			dbf_flush();
		}
		return 1;
	}
	return 0;
}
// clang-format on

ASHE_PRIVATE ubyte TerminalInput_cursor_left(TerminalInput *tinput)
{
	unused(tinput);

	if (COL > 0) {
		COL--;
		if (TCOL == 1) {
			TCOL = TCOLMAX;
			goto lineuptocol;
		} else {
			TCOL--;
			dbf_draw_lit(cursor_left(1));
		}
	} else if (ROW > 0) {
		ROW--;
		COL = LINE.len - 1;
		TCOL = modlen(LINE.len + ((ROW == 0) * PLEN), TCOLMAX);
lineuptocol:
		dbf_pushlit(cursor_up(1));
		dbf_push_movecol(TCOL);
		dbf_flush();
	} else {
		return 0;
	}
	IBFIDX--;
	return 1;
}

ASHE_PRIVATE ubyte TerminalInput_cursor_right(TerminalInput *tinput)
{
	unused(tinput);
	ubyte isnot_lastrow = (LINES.len != 0 && ROW != LINES.len - 1);

	if (COL < LINE.len - isnot_lastrow) {
		COL++;
		if (TCOL < TCOLMAX) {
			TCOL++;
			dbf_draw_lit(cursor_right(1));
		} else {
			goto linedownstart;
		}
	} else if (ROW < (LINES.len - 1)) {
		ROW++;
		COL = 0;
linedownstart:
		TCOL = 1;
		dbf_draw_lit(cursor_down(1) cursor_col(1));
	} else {
		return 0;
	}
	IBFIDX++;
	return 1;
}

ASHE_PRIVATE void TerminalInput_insert(TerminalInput *tinput, int32 c)
{
	memmax i;
	uint32 idx;
	ubyte relink;
	Line *prev = NULL;
	Line *curr = NULL;

	if (IBF.len >= ARG_MAX - 1)
		return;
	idx = IBFIDX;
	relink = (IBF.len >= IBF.cap);
	Buffer_insert(&IBF, IBFIDX, c);
	LINE.len++;
	if (relink) {
		prev = &LINES.data[0];
		prev->start = IBF.data;
		for (i = 1; i < LINES.len; i++) {
			curr = ArrayLine_index(&LINES, i);
			curr->start = prev->start + prev->len;
			prev = curr;
		}
	}
	shift_lines_right(ROW + 1);
	dbf_pushlit(clear_line_right clear_down);
	if (TCOL >= TCOLMAX || c == '\n') {
		dbf_pushc('\n');
		idx++;
	}
	dbf_pushlit(cursor_save);
	dbf_push_len(&IBF.data[idx], IBF.len - idx);
	dbf_pushlit(cursor_load);
	dbf_flush();
	if (c != '\n') /* otherwise we are in 'TerminalInput_cr()' */
		TerminalInput_cursor_right(tinput);
}

ASHE_PRIVATE ubyte TerminalInput_cr(TerminalInput *tinput)
{
	Line newline = { 0 };

	if (is_escaped(IBF.data, IBFIDX) || in_dq(IBF.data, IBFIDX)) {
		TerminalInput_insert(tinput, '\n');
		newline.start = LINE.start + COL + 1;
		newline.len = LINE.len - (COL + 1);
		LINE.len = COL + 1;
		ROW++;
		IBFIDX++;
		COL = 0;
		TCOL = 1;
		ArrayLine_insert(&LINES, ROW, newline);
		return 1;
	}
	return 0;
}

ASHE_PRIVATE void TerminalInput_remove(TerminalInput *tinput)
{
	Line *l;
	ubyte coalesce;

	if (IBFIDX <= 0)
		return;
	Buffer_remove(&IBF, IBFIDX - 1);
	shift_lines_left(ROW + 1);
	LINE.len -= !(coalesce = (COL == 0));
	l = &LINE; /* cache current line */
	TerminalInput_cursor_left(tinput);
	if (coalesce) {
		LINE.len--; /* '\n' */
		LINE.len += l->len;
		ashe_assert(l == &LINES.data[ROW + 1]);
		ashe_assert(l->start == LINES.data[ROW + 1].start);
		ashe_assert(l->len == LINES.data[ROW + 1].len);
		ArrayLine_remove(&LINES, ROW + 1);
	}
	dbf_pushlit(cursor_hide clear_line_right clear_down cursor_save);
	dbf_push_len(IBF.data + IBFIDX, IBF.len - IBFIDX);
	dbf_pushlit(cursor_load cursor_show);
	dbf_flush();
}

ASHE_PUBLIC void TerminalInput_redraw(TerminalInput *tinput)
{
	unused(tinput);
	memmax ridx = LINES.len - 1;
	Line *line = &LINES.data[ridx];
	ssize temp, up;
	uint32 col, len;
	ubyte isfirstrow = 0;

	/* Move up until we find the current row */
	for (up = 0; ridx != ROW; line = &LINES.data[--ridx]) {
		up++;
		temp = line->len - 1;
		/* Make sure we compensate for multiple
		 * terminal rows in a single Line */
		while (temp >= (ssize)TCOLMAX) {
			up++;
			temp -= TCOLMAX;
		}
	}
	isfirstrow = (ROW == 0);
	col = COL + (isfirstrow * PLEN);
	len = LINE.len + (isfirstrow * PLEN);
	/* Find the correct terminal row in the current line */
	up += ((len - 1) / TCOLMAX) - (col / TCOLMAX);
	ashe_assertf(DBF.len == 0, "draw buffer not empty");
	dbf_pushlit(cursor_hide);
	dbf_push_len(IBF.data, IBF.len);
	if (up > 0) {
		dbf_pushlit(CSI);
		dbf_push_unum(up);
		dbf_pushc('A');
	}
	dbf_push_movecol(TCOL);
	dbf_pushlit(cursor_show);
	dbf_flush();
}

ASHE_PRIVATE void TerminalInput_cursor_eol(TerminalInput *tinput)
{
	unused(tinput);
	uint32 extra = (ROW == 0) * PLEN;
	uint32 col = COL + extra;
	uint32 len = LINE.len + extra;
	uint32 temp;

	if (LINE.len > 0 && ((temp = modlen(col + 1, TCOLMAX)) != TCOLMAX ||
			     COL != LINE.len - 1)) {
		if (col / TCOLMAX < (len - 1) / TCOLMAX) {
			TCOL = TCOLMAX;
			COL += TCOLMAX - temp;
			IBFIDX += TCOLMAX - temp;
		} else {
			TCOL = len % TCOLMAX;
			IBFIDX -= COL + 1;
			COL = LINE.len - 1;
		}
		dbf_push_movecol(TCOL);
		dbf_flush();
	}
}

ASHE_PRIVATE void TerminalInput_cursor_home(TerminalInput *tinput)
{
	unused(tinput);
	uint32 extra = (ROW == 0) * PLEN;
	uint32 col = COL + extra;

	if (COL != 0 && col % TCOLMAX != 1) {
		if (col < TCOLMAX) {
			IBFIDX -= COL;
			COL = 0;
			TCOL = extra + 1;
		} else {
			COL -= modlen(col, TCOLMAX) - 1;
			col = COL + extra;
			TCOL = modlen(col, TCOLMAX) + 1;
		}
		dbf_push_movecol(TCOL);
		dbf_flush();
	}
}

ASHE_PUBLIC void TerminalInput_clrscreen(TerminalInput *tinput)
{
	dbf_draw_lit(cursor_hide cursor_home clear_all);
	print_prompt();
	TerminalInput_redraw(tinput);
}

ASHE_PRIVATE enum termkey read_key(void)
{
	ubyte seq[3];
	int32 nread;
	byte c;

	ashe_mask_signals(SIG_UNBLOCK);
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && (errno != EINTR && !ashe.sh_flags.interrupt))
			panic("failed reading terminal input.");
		ashe.sh_flags.interrupt = 0;
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

ASHE_PRIVATE ubyte TerminalInput_process_key(TerminalInput *tinput)
{
	int32 c;

	if (IMPLEMENTED((c = read_key()))) {
		switch (c) {
		case CR:
			if (!TerminalInput_cr(tinput))
				return 0;
			break;
		case DEL_KEY:
		case BACKSPACE:
			TerminalInput_remove(tinput);
			break;
		case END_KEY:
			TerminalInput_cursor_eol(tinput);
			break;
		case HOME_KEY:
			TerminalInput_cursor_home(tinput);
			break;
		case CTRL_KEY('h'): // TODO: change back to 'l' after testing
			TerminalInput_clrscreen(tinput);
			break;
		case L_ARW:
			TerminalInput_cursor_left(tinput);
			break;
		case R_ARW:
			TerminalInput_cursor_right(tinput);
			break;
		case U_ARW:
			TerminalInput_cursor_up(tinput);
			break;
		case D_ARW:
			TerminalInput_cursor_down(tinput);
			break;
		// case CTRL_KEY('h'): // TODO: Uncomment after testing
		case CTRL_KEY('x'):
			_exit(255); // TODO: Remove this after testing
		case CTRL_KEY('j'):
		case CTRL_KEY('k'):
		case CTRL_KEY('i'):
			break;
		default:
			TerminalInput_insert(tinput, c);
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

/* Move cursor to the end of the input */
ASHE_PUBLIC void TerminalInput_goto_input_end(TerminalInput *tinput)
{
	while (TerminalInput_cursor_down(tinput))
		;
	TerminalInput_cursor_eol(tinput);
}

/* Read input from STDIN_FILENO. */
ASHE_PUBLIC void TerminalInput_read(TerminalInput *tinput)
{
	Terminal *term = &ashe.sh_term;
	term->tm_reading = 1;
	set_terminal_mode(&term->tm_rawtermios);
	get_winsize_or_panic(&term->tm_rows, &term->tm_columns);
	get_cursor_pos(NULL, &TCOL);
#ifdef ASHE_DBG_CURSOR
	debug_cursor();
#endif
#ifdef ASHE_DBG_LINES
	debug_lines();
#endif
	WHILE_READING(tinput);
	Buffer_push(&IBF, '\0');
	TerminalInput_goto_input_end(tinput);
	fprintf(stderr, "\r\n");
	fflush(stderr);
	term->tm_reading = 0;
	set_terminal_mode(&term->tm_dfltermios);
	ashe_mask_signals(SIG_UNBLOCK);
}
