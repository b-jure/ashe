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

/* key code defs */
#define CTRL_KEY(k)    ((k) & 0x1f)
#define ESCAPE	       27
#define CR	       0x0D
#define IMPLEMENTED(c) (c != ESCAPE)

/* miscellaneous defs */
#define modlen(x, y)	  ((((x)-1) % (y)) + 1)
#define WHILE_READING(ti) while (TerminalInput_process_key(ti))

/* draw buffer */
#define dbf_draw_lit(strlit) write_or_panic(strlit, sizeof(strlit) - 1)
#define dbf_pushlit(strlit)  dbf_push_len(strlit, sizeof(strlit) - 1)
#define dbf_pushc(c)	     Buffer_push(&DBF, c)
#define dbf_push(s)	     Buffer_push_str(&DBF, s, strlen(s))
#define dbf_push_len(s, len) Buffer_push_str(&DBF, s, len)
#define dbf_push_movecol(n)       \
	do {                      \
		dbf_pushlit(CSI); \
		dbf_push_unum(n); \
		dbf_pushc('G');   \
	} while (0)

/* defines to insure these are inlined */
#define init_dflterm(term) tcgetattr(STDIN_FILENO, term);

#define set_terminal_mode(tmode)                                     \
	if (unlikely(tcsetattr(STDIN_FILENO, TCSAFLUSH, tmode) < 0)) \
		panic(NULL);

#define opost_on()                                              \
	do {                                                    \
		ashe.sh_term.tm_rawtermios.c_oflag |= OPOST;    \
		set_terminal_mode(&ashe.sh_term.tm_rawtermios); \
	} while (0)

#define opost_off()                                             \
	do {                                                    \
		ashe.sh_term.tm_rawtermios.c_oflag &= ~(OPOST); \
		set_terminal_mode(&ashe.sh_term.tm_rawtermios); \
	} while (0)

#define shift_lines_right(row)                   \
	for (uint32 i = row; i < LINES.len; i++) \
		LINES.data[i].start++;

#define shift_lines_left(row)                    \
	for (uint32 i = row; i < LINES.len; i++) \
		LINES.data[i].start--;

/* Implemented keys */
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
	opost_on();
	write_or_panic(drawbuf->data, drawbuf->len);
	opost_off();
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
	}
	if (height)
		*height = ws.ws_row;
	if (width)
		*width = ws.ws_col;
	return 0;
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

ASHE_PRIVATE ubyte TerminalInput_cursor_up(TerminalInput *tinput)
{
	unused(tinput);
	uint32 extra = ((ROW == 0) * PLEN);
	uint32 len = COL + 1 + extra;
	uint32 pwraps = (PLEN != 0) * ((PLEN - 1) / TCOLMAX);
	uint32 lwraps = (len - 1) / TCOLMAX;
	uint32 temp;

	temp = lwraps - pwraps;
	if (ROW == 0 && (temp == 0 || (temp < 2 && len % TCOLMAX == 0)))
		return 0;
	if (ROW == 0) {
		if (temp == 1 &&
		    (temp = modlen(PLEN, TCOLMAX)) >= modlen(len, TCOLMAX)) {
start:
			COL = 0;
			IBFIDX = 0;
			TCOL = temp + 1;
			goto uptocol_draw;
		} else {
			goto up;
		}
	} else if (lwraps == 0) {
		IBFIDX -= COL + 1; /* end of the line above */
		ROW--;
		extra = (ROW == 0) * PLEN;
		len = LINE.len + extra;
		lwraps = (len - 1) / TCOLMAX;
		if ((temp = modlen(len, TCOLMAX)) <= COL + 1) {
			COL = LINE.len - 1;
			TCOL = temp;
			goto uptocol_draw;
		} else {
			temp = modlen(COL + 1, TCOLMAX);
			if (ROW == 0 && lwraps - pwraps == 0 &&
			    modlen(PLEN, TCOLMAX) >= temp) {
				temp = modlen(PLEN + 1, TCOLMAX);
				goto start;
			}
			temp = modlen(len, TCOLMAX) - temp;
			COL = LINE.len - temp - 1;
			IBFIDX -= temp;
			goto up_draw;
		}
	} else { /* lwraps > 0 */
up:
		COL -= TCOLMAX;
		IBFIDX -= TCOLMAX;
up_draw:
		dbf_pushlit(cursor_up(1));
		goto flush;
	}
uptocol_draw:
	dbf_pushlit(cursor_up(1));
	dbf_push_movecol(TCOL);
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
	uint32 colwraps = (COL != 0) * ((COL + extra) / TCOLMAX);
	ssize wrapdepth = linewraps - colwraps;
	ubyte temp; /* for edge cases */

	if (wrapdepth > 0) {
		if (wrapdepth > 1 || COL + extra + TCOLMAX <= LINE.len + extra - 1) {
			COL += TCOLMAX;
			IBFIDX += TCOLMAX;
			goto down;
		} else {
			temp = (ROW != LINES.len - 1);
			IBFIDX += LINE.len - COL - temp;
			COL = LINE.len - temp;
			TCOL = modlen(LINE.len + extra + !temp, TCOLMAX);
			goto downtocol;
		}
	} else if (ROW < LINES.len - 1) {
		IBFIDX += LINE.len - COL; /* start of new line */
		ROW++;
		if (LINE.len >= TCOLMAX || LINE.len >= modlen(COL + 1 + extra, TCOLMAX)) {
			IBFIDX += TCOL - 1;
			COL = TCOL - 1;
down:
			dbf_pushlit(cursor_down(1));
			goto flush;
		} else {
			temp = (ROW != LINES.len - 1);
			IBFIDX += LINE.len - temp;
			COL = LINE.len - temp;
			TCOL = LINE.len + !temp;
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
	if (COL < LINE.len - (LINES.len != 0 && ROW != LINES.len - 1)) {
		COL++;
		if (TCOL < TCOLMAX) {
			TCOL++;
			dbf_draw_lit(cursor_right(1));
		} else {
			goto firstcoldown;
		}
	} else if (ROW < (LINES.len - 1)) {
		ROW++;
		COL = 0;
firstcoldown:
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
	prev = &LINES.data[0];
	prev->start = IBF.data;
	for (i = 1; i < relink * LINES.len; i++) {
		curr = ArrayLine_index(&LINES, i);
		curr->start = prev->start + prev->len;
		prev = curr;
	}
	shift_lines_right(ROW + 1);
	dbf_pushlit(clear_line_right clear_down);
	if (c == '\n' && TCOL < TCOLMAX) {
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
