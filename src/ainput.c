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

/* control sequence introducer */
#define A_CSI	   "\033["
#define A_ESC(seq) A_CSI #seq
/* cursor */
#define a_csi_cursor_home	   A_ESC(H)
#define a_csi_cursor_left(n)	   A_ESC(n) "D"
#define c_si_cursor_right(n)	   A_ESC(n) "C"
#define c_si_cursor_up(n)	   A_ESC(n) "A"
#define c_si_cursor_down(n)	   A_ESC(n) "B"
#define c_si_cursor_save	   A_ESC(s)
#define c_si_cursor_load	   A_ESC(u)
#define c_si_cursor_col(col)	   A_ESC(col) "G"
#define c_si_cursor_move(row, col) A_ESC(row ";" col) "H"
#define c_si_cursor_position	   A_ESC(6n)
#define c_si_cursor_hide A_ESC(?25l)
#define c_si_cursor_show A_ESC(?25h)
/* clear */
#define c_si_clear_down	      A_ESC(0J)
#define c_si_clear_up	      A_ESC(1J)
#define c_si_clear_all	      A_ESC(2J)
#define c_si_clear_line_right A_ESC(0K)
#define c_si_clear_line_left  A_ESC(1K)
#define c_si_clear_line	      A_ESC(2K)

/* key code defs */
#define CTRL_KEY(k)    ((k) & 0x1f)
#define ESCAPE	       27
#define CR	       0x0D
#define IMPLEMENTED(c) (c != ESCAPE)

/* miscellaneous defs */
#define modlen(x, y) ((((x)-1) % (y)) + 1)

/* draw buffer */
#define dbf_draw_lit(strlit) write_or_panic(strlit, sizeof(strlit) - 1)
#define dbf_pushlit(strlit)  dbf_push_len(strlit, sizeof(strlit) - 1)
#define dbf_pushc(c)	     a_arr_char_push(&A_DBF, c)
#define dbf_push(s)	     a_arr_char_push_str(&A_DBF, s, strlen(s))
#define dbf_push_len(s, len) a_arr_char_push_str(&A_DBF, s, len)
#define dbf_push_movecol(n)         \
	do {                        \
		dbf_pushlit(A_CSI); \
		dbf_push_unum(n);   \
		dbf_pushc('G');     \
	} while (0)
#define dbf_push_moveup(n)          \
	do {                        \
		dbf_pushlit(A_CSI); \
		dbf_push_unum(n);   \
		dbf_pushc('A');     \
	} while (0)

/* defines to insure these are inlined */
#define init_dflterm(term) tcgetattr(STDIN_FILENO, term);

#define set_terminal_mode(tmode)                                     \
	if (unlikely(tcsetattr(STDIN_FILENO, TCSAFLUSH, tmode) < 0)) \
		ashe_panic("can't set terminal settings");

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

#define shift_lines_right(row)                     \
	for (uint32 i = row; i < A_LINES.len; i++) \
		A_LINES.data[i].start++;

#define shift_lines_left(row)                      \
	for (uint32 i = row; i < A_LINES.len; i++) \
		A_LINES.data[i].start--;

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

ASHE_PRIVATE inline void write_or_panic(const char *ptr, memmax n)
{
	if (unlikely(ashe_write(STDERR_FILENO, ptr, n) < 0))
		ashe_panic(NULL);
	fflush(stderr);
	if (unlikely(ferror(stderr))) {
		ashe_perrno("stderr");
		ashe_panic(NULL);
	}
}

ASHE_PRIVATE inline void dbf_push_unum(memmax n)
{
	char temp[UINT_DIGITS];
	int32 chars;

	if (unlikely((chars = snprintf(temp, UINT_DIGITS, "%zu", n)) < 0)) {
		ashe_perrno("snprintf");
		ashe_panic(NULL);
	}
	a_arr_char_push_str(&A_DBF, temp, chars);
}

ASHE_PRIVATE inline void dbf_flush()
{
	a_arr_char *drawbuf = &A_DBF;
	opost_on();
	write_or_panic(drawbuf->data, drawbuf->len);
	opost_off();
	drawbuf->len = 0;
}

ASHE_PRIVATE int32 get_winsize_fallback(uint32 *height, uint32 *width)
{
	int32 res;

	dbf_draw_lit(c_si_cursor_save c_si_cursor_right(99999)
			     c_si_cursor_down(99999));
	res = ashe_get_curpos(height, width);
	dbf_draw_lit(c_si_cursor_load);
	return res;
}

ASHE_PUBLIC int32 ashe_get_winsize(uint32 *height, uint32 *width)
{
	struct winsize ws;

	if (unlikely(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 ||
		     ws.ws_col == 0)) {
		return get_winsize_fallback(height, width);
	}
	if (height)
		*height = ws.ws_row;
	if (width)
		*width = ws.ws_col;
	return 0;
}

ASHE_PUBLIC int32 ashe_get_curpos(uint32 *row, uint32 *col)
{
	char c;
	memmax i = 0;
	int32 nread;
	uint32 srow, scol;
	char buf[INT_DIGITS * 2 + sizeof(A_CSI ";")]; /* A_ESC [ Pn ; Pn R */

	dbf_draw_lit(c_si_cursor_position);
	while ((nread = read(STDIN_FILENO, &c, 1)) == 1) {
		if (c == 'R')
			break;
		buf[i++] = c;
	}
	if (nread == -1 || sscanf(buf, "\033[%u;%u", &srow, &scol) != 2) {
		ashe_perrno("sscanf");
		return -1;
	}
	if (row)
		*row = srow;
	if (col)
		*col = scol;
	return 0;
}

/* Auxiliary to 'a_term_init()' */
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

ASHE_PUBLIC void a_terminput_init(void)
{
	a_arr_char_init_cap(&A_IBF, 8);
	a_arr_char_init_cap(&A_DBF, 8);
	a_arr_line_init(&A_LINES);
	a_arr_line_push(&A_LINES,
			(struct a_line){ .len = 0, .start = A_IBF.data });
	A_IBFIDX = 0;
	A_COL = 0;
	A_ROW = 0;
}

ASHE_PUBLIC void a_terminput_free(void)
{
	a_arr_char_free(&A_IBF, NULL);
	a_arr_char_free(&A_DBF, NULL);
	a_arr_line_free(&A_LINES, NULL);
}

ASHE_PUBLIC void a_term_init(void)
{
	a_terminput_init();
	init_dflterm(&A_TM.tm_dfltermios);
	init_rawterm(&A_TM.tm_rawtermios);
	ashe_get_winsize_or_panic(&A_TM.tm_rows, &A_TM.tm_columns);
	/* tm_col - gets set when reading and drawing */
	/* tm_promptlen - gets set in 'print_prompt()' */
	A_TM.tm_reading = 0;
}

ASHE_PUBLIC ubyte ashe_cursor_up(void)
{
	uint32 extra = ((A_ROW == 0) * A_PLEN);
	uint32 len = A_COL + 1 + extra;
	uint32 pwraps = (A_PLEN != 0) * ((A_PLEN - 1) / A_TCOLMAX);
	uint32 lwraps = (len - 1) / A_TCOLMAX;
	uint32 temp;

	temp = lwraps - pwraps;
	if (A_ROW == 0 && (temp == 0 || (temp < 2 && len % A_TCOLMAX == 0)))
		return 0;
	if (A_ROW == 0) {
		if (temp == 1 && (temp = modlen(A_PLEN, A_TCOLMAX)) >=
					 modlen(len, A_TCOLMAX)) {
start:
			A_COL = 0;
			A_IBFIDX = 0;
			A_TCOL = temp + 1;
			goto uptocol_draw;
		} else {
			goto up;
		}
	} else if (lwraps == 0) {
		A_IBFIDX -= A_COL + 1; /* end of the line above */
		A_ROW--;
		extra = (A_ROW == 0) * A_PLEN;
		len = A_LINE.len + extra;
		lwraps = (len - 1) / A_TCOLMAX;
		if ((temp = modlen(len, A_TCOLMAX)) <= A_COL + 1) {
			A_COL = A_LINE.len - 1;
			A_TCOL = temp;
			goto uptocol_draw;
		} else {
			temp = modlen(A_COL + 1, A_TCOLMAX);
			if (A_ROW == 0 && lwraps - pwraps == 0 &&
			    modlen(A_PLEN, A_TCOLMAX) >= temp) {
				temp = modlen(A_PLEN + 1, A_TCOLMAX);
				goto start;
			}
			temp = modlen(len, A_TCOLMAX) - temp;
			A_COL = A_LINE.len - temp - 1;
			A_IBFIDX -= temp;
			goto up_draw;
		}
	} else { /* lwraps > 0 */
up:
		A_COL -= A_TCOLMAX;
		A_IBFIDX -= A_TCOLMAX;
up_draw:
		dbf_pushlit(c_si_cursor_up(1));
		goto flush;
	}
uptocol_draw:
	dbf_pushlit(c_si_cursor_up(1));
	dbf_push_movecol(A_TCOL);
flush:
	dbf_flush();
	return 1;
}

// clang-format off
ASHE_PUBLIC ubyte ashe_cursor_down(void)
{
	uint32 extra = (A_ROW == 0) * A_PLEN;
	uint32 linewraps = (A_LINE.len != 0) * ((A_LINE.len - 1 + extra) / A_TCOLMAX);
	uint32 colwraps = (A_COL != 0) * ((A_COL + extra) / A_TCOLMAX);
	ssize wrapdepth = linewraps - colwraps;
	ubyte temp; /* for edge cases */

	if (wrapdepth > 0) {
		if (wrapdepth > 1 || A_COL + extra + A_TCOLMAX <= A_LINE.len + extra - 1) {
			A_COL += A_TCOLMAX;
			A_IBFIDX += A_TCOLMAX;
			goto down;
		} else {
			temp = (A_ROW != A_LINES.len - 1);
			A_IBFIDX += A_LINE.len - A_COL - temp;
			A_COL = A_LINE.len - temp;
			A_TCOL = modlen(A_LINE.len + extra + !temp, A_TCOLMAX);
			goto downtocol;
		}
	} else if (A_ROW < A_LINES.len - 1) {
		A_IBFIDX += A_LINE.len - A_COL; /* start of new line */
		A_ROW++;
		if (A_LINE.len >= A_TCOLMAX || A_LINE.len >= modlen(A_COL + 1 + extra, A_TCOLMAX)) {
			A_IBFIDX += A_TCOL - 1;
			A_COL = A_TCOL - 1;
down:
			dbf_pushlit(c_si_cursor_down(1));
			goto flush;
		} else {
			temp = (A_ROW != A_LINES.len - 1);
			A_IBFIDX += A_LINE.len - temp;
			A_COL = A_LINE.len - temp;
			A_TCOL = A_LINE.len + !temp;
downtocol:
			dbf_pushlit(c_si_cursor_down(1));
			dbf_push_movecol(A_TCOL);
flush:
			dbf_flush();
		}
		return 1;
	}
	return 0;
}
// clang-format on

ASHE_PUBLIC ubyte ashe_cursor_left(void)
{
	if (A_COL > 0) {
		A_COL--;
		if (A_TCOL == 1) {
			A_TCOL = A_TCOLMAX;
			goto lineuptocol;
		} else {
			A_TCOL--;
			dbf_draw_lit(a_csi_cursor_left(1));
		}
	} else if (A_ROW > 0) {
		A_ROW--;
		A_COL = A_LINE.len - 1;
		A_TCOL =
			modlen(A_LINE.len + ((A_ROW == 0) * A_PLEN), A_TCOLMAX);
lineuptocol:
		dbf_pushlit(c_si_cursor_up(1));
		dbf_push_movecol(A_TCOL);
		dbf_flush();
	} else {
		return 0;
	}
	A_IBFIDX--;
	return 1;
}

ASHE_PUBLIC ubyte ashe_cursor_right(void)
{
	if (A_COL <
	    A_LINE.len - (A_LINES.len != 0 && A_ROW != A_LINES.len - 1)) {
		A_COL++;
		if (A_TCOL < A_TCOLMAX) {
			A_TCOL++;
			dbf_draw_lit(c_si_cursor_right(1));
		} else {
			goto firstcoldown;
		}
	} else if (A_ROW < (A_LINES.len - 1)) {
		A_ROW++;
		A_COL = 0;
firstcoldown:
		A_TCOL = 1;
		dbf_draw_lit(c_si_cursor_down(1) c_si_cursor_col(1));
	} else {
		return 0;
	}
	A_IBFIDX++;
	return 1;
}

ASHE_PUBLIC ubyte ashe_insert(int32 c)
{
	memmax i;
	uint32 idx;
	ubyte relink;
	struct a_line *prev = NULL;
	struct a_line *curr = NULL;

	if (A_IBF.len >= ARG_MAX - 1)
		return 0;
	idx = A_IBFIDX;
	relink = (A_IBF.len >= A_IBF.cap);
	a_arr_char_insert(&A_IBF, A_IBFIDX, c);
	A_LINE.len++;
	prev = &A_LINES.data[0];
	prev->start = A_IBF.data;
	for (i = 1; i < relink * A_LINES.len; i++) {
		curr = a_arr_line_index(&A_LINES, i);
		curr->start = prev->start + prev->len;
		prev = curr;
	}
	shift_lines_right(A_ROW + 1);
	dbf_pushlit(c_si_clear_line_right c_si_clear_down);
	if (c == '\n' && A_TCOL < A_TCOLMAX) {
		dbf_pushc('\n');
		idx++;
	}
	dbf_pushlit(c_si_cursor_save);
	dbf_push_len(&A_IBF.data[idx], A_IBF.len - idx);
	dbf_pushlit(c_si_cursor_load);
	dbf_flush();
	if (c != '\n') /* otherwise we are in 'a_terminput_cr()' */
		ashe_cursor_right();
	return 1;
}

ASHE_PUBLIC ubyte ashe_cr(void)
{
	struct a_line newline = { 0 };

	if (ashe_isescaped(A_IBF.data, A_IBFIDX) ||
	    ashe_indq(A_IBF.data, A_IBFIDX)) {
		ashe_insert('\n');
		newline.start = A_LINE.start + A_COL + 1;
		newline.len = A_LINE.len - (A_COL + 1);
		A_LINE.len = A_COL + 1;
		A_ROW++;
		A_IBFIDX++;
		A_COL = 0;
		A_TCOL = 1;
		a_arr_line_insert(&A_LINES, A_ROW, newline);
		return 1;
	}
	return 0;
}

ASHE_PUBLIC ubyte ashe_remove(void)
{
	struct a_line *l;
	ubyte coalesce;

	if (A_IBFIDX <= 0)
		return 0;
	a_arr_char_remove(&A_IBF, A_IBFIDX - 1);
	shift_lines_left(A_ROW + 1);
	A_LINE.len -= !(coalesce = (A_COL == 0));
	l = &A_LINE; /* cache current line */
	ashe_cursor_left();
	if (coalesce) {
		A_LINE.len--; /* '\n' */
		A_LINE.len += l->len;
		ashe_assert(l == &A_LINES.data[A_ROW + 1]);
		ashe_assert(l->start == A_LINES.data[A_ROW + 1].start);
		ashe_assert(l->len == A_LINES.data[A_ROW + 1].len);
		a_arr_line_remove(&A_LINES, A_ROW + 1);
	}
	dbf_pushlit(c_si_cursor_hide c_si_clear_line_right c_si_clear_down
			    c_si_cursor_save);
	dbf_push_len(A_IBF.data + A_IBFIDX, A_IBF.len - A_IBFIDX);
	dbf_pushlit(c_si_cursor_load c_si_cursor_show);
	dbf_flush();
	return 1;
}

ASHE_PUBLIC ubyte ashe_cursor_lineend(void)
{
	uint32 extra = (A_ROW == 0) * A_PLEN;
	uint32 col = A_COL + extra;
	uint32 len = A_LINE.len + extra;
	uint32 temp;
	ubyte lastrow = (A_ROW == A_LINES.len - 1);

	if (A_LINE.len > 0 &&
	    ((temp = modlen(col + 1, A_TCOLMAX)) != A_TCOLMAX ||
	     A_COL != A_LINE.len - !lastrow)) {
		if (col / A_TCOLMAX < (len - 1) / A_TCOLMAX) {
			A_TCOL = A_TCOLMAX;
			A_COL += A_TCOLMAX - temp;
			A_IBFIDX += A_TCOLMAX - temp;
		} else {
			A_TCOL = (len % A_TCOLMAX) + lastrow;
			A_IBFIDX += A_LINE.len - A_COL - !lastrow;
			A_COL = A_LINE.len - !lastrow;
		}
		dbf_push_movecol(A_TCOL);
		dbf_flush();
		return 1;
	}
	return 0;
}

ASHE_PUBLIC ubyte ashe_cursor_linestart(void)
{
	uint32 extra = (A_ROW == 0) * A_PLEN;
	uint32 col = A_COL + extra;
	ssize temp;

	if (A_COL != 0 && (temp = modlen(col + 1, A_TCOLMAX)) != 1) {
		if (col < A_TCOLMAX) {
			A_IBFIDX -= A_COL;
			A_COL = 0;
			A_TCOL = extra + 1;
		} else {
			A_IBFIDX -= temp - 1;
			A_COL -= temp - 1;
			col = A_COL + extra;
			A_TCOL = modlen(col + 1, A_TCOLMAX);
		}
		dbf_push_movecol(A_TCOL);
		dbf_flush();
		return 1;
	}
	return 0;
}

ASHE_PUBLIC void ashe_redraw(void)
{
	uint32 col = A_COL + (A_ROW == 0) * A_PLEN;

	A_TCOL = modlen(col + 1, A_TCOLMAX);
	dbf_pushlit(c_si_cursor_hide);
	dbf_push_len(A_IBF.data, A_IBFIDX);
	dbf_pushlit(c_si_cursor_save);
	dbf_push_len(A_IBF.data + A_IBFIDX, A_IBF.len - A_IBFIDX);
	dbf_pushlit(c_si_cursor_load c_si_cursor_show);
	dbf_flush();
}

ASHE_PUBLIC void ashe_clear_screen_raw(void)
{
	dbf_draw_lit(a_csi_cursor_home c_si_clear_all);
}

ASHE_PUBLIC void ashe_clear_screen(void)
{
	dbf_draw_lit(a_csi_cursor_home c_si_clear_all);
	ashe_pprompt();
	ashe_redraw();
}

/* Move cursor to the end of the input */
ASHE_PUBLIC void ashe_cursor_end(void)
{
	while (ashe_cursor_down())
		;
	ashe_cursor_lineend();
}

ASHE_PRIVATE enum termkey read_key(void)
{
	ubyte seq[3];
	int32 nread;
	byte c;

	ashe_mask_signals(SIG_UNBLOCK);
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (unlikely(nread == -1 && (errno != EINTR && !ashe.sh_int))) {
			ashe_mask_signals(SIG_BLOCK);
			ashe_panic("couldn't read terminal input.");
		}
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

ASHE_PRIVATE ubyte process_key(void)
{
	int32 c;

	if (IMPLEMENTED((c = read_key()))) {
		switch (c) {
		case CR:
			if (!ashe_cr())
				return 0;
			break;
		case DEL_KEY:
		case BACKSPACE:
			ashe_remove();
			break;
		case END_KEY:
			ashe_cursor_lineend();
			break;
		case HOME_KEY:
			ashe_cursor_linestart();
			break;
		case CTRL_KEY('h'): // TODO: change back to 'l' after testing
			ashe_clear_screen();
			break;
		case L_ARW:
			ashe_cursor_left();
			break;
		case R_ARW:
			ashe_cursor_right();
			break;
		case U_ARW:
			ashe_cursor_up();
			break;
		case D_ARW:
			ashe_cursor_down();
			break;
		// case CTRL_KEY('h'): // TODO: Uncomment after testing
		case CTRL_KEY('x'):
			_exit(255); // TODO: Remove this after testing
		case CTRL_KEY('j'):
		case CTRL_KEY('k'):
		case CTRL_KEY('i'):
			break;
		default:
			if (isgraph(c) || isspace(c))
				ashe_insert(c);
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

/* Read input from STDIN_FILENO. */
ASHE_PUBLIC void a_terminput_read(void)
{
	ashe_assert(A_IBF.data != NULL);
	ashe_assert(A_DBF.data != NULL);
	ashe_mask_signals(SIG_BLOCK);
	A_TM.tm_reading = 1;
	set_terminal_mode(&A_TM.tm_rawtermios);
	ashe_get_winsize_or_panic(&A_TM.tm_rows, &A_TM.tm_columns);
	ashe_get_curpos(NULL, &A_TCOL);
#ifdef ASHE_DBG_CURSOR
	debug_cursor();
#endif
#ifdef ASHE_DBG_LINES
	debug_lines();
#endif
	while (process_key())
		;
	a_arr_char_push(&A_IBF, '\0');
	ashe_cursor_end();
	ashe_print("\r\n", stderr);
	A_TM.tm_reading = 0;
	set_terminal_mode(&A_TM.tm_dfltermios);
}
