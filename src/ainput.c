#include "autils.h"
#include "aerrors.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "ashell.h"

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#define MAXNAME		    UINT8_MAX
#define ASHE_PROMPT_LEN_MAX ((ARG_MAX >> 4) ? (ARG_MAX >> 4) : 1024)

typedef enum {
	BACKSPACE = 127,
	L_ARW = 1000,
	U_ARW,
	D_ARW,
	R_ARW,
	HOME_KEY,
	END_KEY,
	DEL_KEY
} termkey;

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESCAPE	    27
#define CR	    0x0D

#define IMPLEMENTED(c)	(c != ESCAPE)
#define modlen(x, y)	((((x)-1) % (y)) + 1)
#define remainder(x, y) ((x) % (y))

#define dbf_draw_lit(strlit) write_or_panic(strlit, sizeof(strlit) - 1)
#define dbf_pushlit(strlit)  dbf_push_len(strlit, sizeof(strlit) - 1)
#define dbf_push_movecol(n)       \
	do {                      \
		dbf_pushlit(CSI); \
		dbf_push_unum(n); \
		dbf_pushc('G');   \
	} while (0)

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

#define WRAPS(len) ((len) >= TCOLMAX)

static finline void set_terminal_mode(struct termios *tmode)
{
	if (unlikely(tcsetattr(STDIN_FILENO, TCSAFLUSH, tmode) < 0))
		panic(NULL);
}

static finline void turn_on_nltrans(void)
{
	ashe.sh_term.tm_rawtermios.c_oflag |= (OPOST);
	set_terminal_mode(&ashe.sh_term.tm_rawtermios);
}

static finline void turn_off_nltrans(void)
{
	ashe.sh_term.tm_rawtermios.c_oflag &= ~(OPOST);
	set_terminal_mode(&ashe.sh_term.tm_rawtermios);
}

static finline void dbf_pushc(char c)
{
	Buffer_push(&DBF, c);
}

static finline void dbf_push(const char *s)
{
	Buffer_push_str(&DBF, s, strlen(s));
}

static finline void dbf_push_len(const char *s, memmax len)
{
	Buffer_push_str(&DBF, s, len);
}

static finline void dbf_push_unum(memmax n)
{
	ubyte chars;

	Buffer_ensure(&DBF, UINT_DIGITS);
	if (unlikely(chars = snprintf(DBF.data, UINT_DIGITS, "%zu", n) < 0)) {
		print_errno();
		panic(NULL);
	}
	DBF.len += chars;
}

static finline void dbf_flush()
{
	Buffer *drawbuf = &DBF;
	Buffer_push(drawbuf, '\0');
	turn_on_nltrans();
	write_or_panic(drawbuf->data, drawbuf->len);
	turn_off_nltrans();
	drawbuf->len = 0;
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

int32 get_cursor_pos(uint32 *row, uint32 *col)
{
	int32 c;
	memmax i = 0;
	int32 nread;
	uint32 srow, scol;
	char buf[10 * 2 + sizeof(CSI ";")]; /* ESC [ Pn ; Pn R */

	dbf_draw_lit(cursor_position);

	while ((nread = read(STDIN_FILENO, &c, 1)) == 1) {
		if (c == 'R')
			break;
		buf[i++] = c;
	}
	if (nread == -1 || sscanf(buf, "\e[%u;%u", &srow, &scol) != 2) {
		print_errno();
		return -1;
	}
	if (row)
		*row = srow;
	if (col)
		*col = scol;
	return 0;
}

int32 get_window_size_fallback(uint32 *height, uint32 *width)
{
	dbf_draw_lit(cursor_save cursor_right(99999) cursor_down(99999)
			     cursor_position cursor_load);
	return get_cursor_pos(height, width);
}

static void expand_placeholders(Buffer *buffer, char *sysname, char *username,
				uint32 jobcount)
{
	static const int32 delimiter = '%';

	char *base = buffer->data;
	char *ptr = base;
	char *start = NULL;
	char *end = NULL;
	const byte *pholder = NULL;

	// TODO: Implement
}

void print_prompt(void)
{
	static char username[MAXNAME] = { 0 };
	Buffer prompt;
	struct passwd pwd;
	struct utsname system;

	memmax jobn = JobControl_jobs(&ashe.sh_jobcntl);

	if (unlikely(uname(&system) < 0)) {
		/* TODO: errmsg */
		panic(NULL);
	} else if (unlikely(getlogin_r(username, MAXNAME) < 0)) {
		/* TODO: errmsg */
		panic(NULL);
	}

	// TODO: Implement
}

/* Auxiliary to 'Terminal_init()' */
static void init_rawterm(struct termios *rawterm)
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
static finline void init_dflterm(struct termios *dflterm)
{
	tcgetattr(STDIN_FILENO, dflterm);
}

static void TerminalInput_init(TerminalInput *tinput)
{
	Buffer_free(&IBF, NULL);
	Buffer_init_cap(&IBF, 8);
	Buffer_free(&DBF, NULL);
	Buffer_init_cap(&IBF, 8);
	ArrayLine_free(&LINES, NULL);
	ArrayLine_push(&LINES, (Line){ .len = 0, .start = IBF.data });
	IBFIDX = 0;
	COL = 0;
	ROW = 0;
}

static void TerminalInput_free(TerminalInput *tinput)
{
	Buffer_free(&IBF, NULL);
	Buffer_free(&DBF, NULL);
	ArrayLine_free(&tinput->in_lines, NULL);
}

void Terminal_init(Terminal *term)
{
	TerminalInput_init(&term->tm_input);
	init_rawterm(&term->tm_rawtermios);
	init_dflterm(&term->tm_dfltermios);
	get_winsize_or_panic(&term->tm_rows, &term->tm_columns);
	/* tm_col - gets set when reading and drawing */
	/* tm_promptlen - gets set in 'print_prompt()' */
	term->tm_reading = 0;
}

void Terminal_free(Terminal *term)
{
	TerminalInput_free(&term->tm_input);
}

static void shift_lines_right(memmax start)
{
	memmax i;
	ArrayLine *lns = &LINES;
	for (i = start; i < lns->len; i++)
		lns->data[i].start++;
}

static void shift_lines_left(memmax start)
{
	memmax i;
	ArrayLine *lns = &LINES;
	for (i = start; i < lns->len; i++)
		lns->data[i].start--;
}

static ubyte TerminalInput_cursor_up(TerminalInput *tinput)
{
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
				IBFIDX -= COL + 1;
				COL = LINE.len - 1;
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
				IBFIDX -= COL + 1;
				COL = LINE.len - 1;
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
static ubyte TerminalInput_cursor_down(TerminalInput *tinput)
{
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

static ubyte TerminalInput_cursor_left(TerminalInput *tinput)
{
	if (COL > 0) {
		COL--;
		if (TCOL == 1) {
			TCOL = TCOLMAX;
			goto lineuptocol;
		} else {
			TCOL--;
			dbf_draw_lit(cursor_left(1));
			goto flush;
		}
	} else if (ROW > 0) {
		ROW--;
		COL = LINE.len - 1;
		TCOL = modlen(LINE.len + ((ROW == 0) * PLEN), TCOLMAX);
lineuptocol:
		dbf_pushlit(cursor_up(1));
		dbf_push_movecol(TCOL);
flush:
		dbf_flush();
	} else {
		return 0;
	}
	IBFIDX--;
	return 1;
}

static ubyte TerminalInput_cursor_right(TerminalInput *buffer)
{
	if (COL < LINE.len - 1) {
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

static void TerminalInput_insert(TerminalInput *tinput, int32 c)
{
	if (IBF.len >= ARG_MAX - 1)
		return;
	Buffer_insert(&IBF, IBFIDX, c);
	LINE.len++;
	shift_lines_right(ROW + 1);
	dbf_pushlit(cursor_hide clear_line_right clear_down);
	if (TCOL >= TCOLMAX || c == '\n')
		dbf_push("\r\n");
	dbf_pushlit(cursor_save);
	dbf_push_len(&IBF.data[IBFIDX], IBF.len - IBFIDX);
	dbf_pushlit(cursor_load cursor_show);
	dbf_flush();
	if (c != '\n') /* otherwise 'TerminalInput_cr()' handles it */
		TerminalInput_cursor_right(tinput);
}

static ubyte TerminalInput_cr(TerminalInput *tinput)
{
	Line newline = { 0 };

	if (is_escaped(LINE.start, COL) || in_dq(IBF.data, IBFIDX)) {
		newline.start = LINE.start + COL;
		newline.len = LINE.len - COL;
		LINE.len = COL;
		TerminalInput_insert(tinput, '\n');
		ROW++;
		IBFIDX++;
		COL = 0;
		TCOL = 1;
		ArrayLine_insert(&LINES, ROW, newline);
		return 1;
	}
	return 0;
}

static void TerminalInput_remove(TerminalInput *tinput)
{
	Line *l;
	ubyte coalesce;

	if (IBFIDX <= 0)
		return;
	coalesce = COL == 0;
	Buffer_remove(&IBF, IBFIDX);
	shift_lines_left(ROW + 1);
	LINE.len--;
	l = &LINE; /* cache current line */
	TerminalInput_cursor_left(tinput); /* updates ROW,COL,IBFIDX and TCOL */
	if (coalesce) {
		LINE.len += l->len;
		ArrayLine_remove(&LINES, ROW + 1);
	}
	dbf_pushlit(cursor_hide clear_line_right clear_down cursor_save);
	dbf_push_len(LINE.start + COL, IBF.len - IBFIDX);
	dbf_pushlit(cursor_load cursor_show);
	dbf_flush();
}

void TerminalInput_redraw(TerminalInput *buffer)
{
	memmax ridx = LINES.len - 1;
	Line *line = &LINES.data[ridx];
	ssize temp, up;
	uint32 col, len;
	ubyte isfirstrow = 0;
	char num[UINT_DIGITS];

	/* Move up until we find the current row */
	for (up = 0; ridx != ROW; line = &LINES.data[--ridx]) {
		up++;
		temp = line->len - 1;
		/* Make sure we compensate for multiple
		 * terminal rows in a single buffer row */
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
	ashe_assert(DBF.len == 0, "draw buffer not empty");
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

void TerminalInput_eol(TerminalInput *buffer)
{
	uint32 extra = (ROW == 0) * PLEN;
	uint32 col = COL + extra;
	uint32 len = LINE.len + extra;
	uint32 temp;

	if (LINE.len > 0 && ((temp = modlen(col + 1, TCOLMAX)) != TCOLMAX ||
			     COL != LINE.len - 1)) {
		if (col / TCOLMAX < (len - 1) / TCOLMAX) {
			TCOL = TCOLMAX;
			COL += TCOLMAX - modlen(col + 1, TCOLMAX);
		} else {
			TCOL = len % TCOLMAX;
			COL = LINE.len - 1;
		}
		dbf_push_movecol(TCOL);
		dbf_flush();
	}
}

void goto_home(TerminalInput *buffer)
{
	uint32 extra = (ROW == 0) * PLEN;
	uint32 col = COL + extra;

	/* TODO: Continue from here */
	if (COL != 0 && col % TCOLMAX != 0) {
		if (col < TCOLMAX) {
			COL = 0;
			TCOL = extra + 1;
		} else {
			COL = modlen(col, TCOLMAX) - 1;
			cursor->cr_col -= remainder(current_column, maxcol);
			terminal.tm_col = 1;
		}

		byte out[sizeof(CSI "G+") + UINT_DIGITS];
		out[0] = '\0';
		write_or_panic(out, sprintf(out, CSI "%uG", terminal.tm_col));
	}
}

static void clear_screen(TerminalInput *buffer)
{
	write_or_panic(cursor_hide cursor_home clear_all,
		       sizeof(cursor_hide clear_all cursor_home));
	pprompt();
	TerminalInput_redraw(buffer);
}

static int32 read_key(void)
{
	int32 nread;
	int32 c;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && (errno != EINTR && !shell.sh_intr)) {
			ATOMIC_PRINT({
				PW_TERMINR;
				die();
			});
		}
	}

	mask_signal(SIGCHLD, SIG_BLOCK);
	mask_signal(SIGINT, SIG_BLOCK);
	mask_signal(SIGWINCH, SIG_BLOCK);

	if (c == ESCAPE) {
		ubyte seq[3];

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

static ubyte inbuff_process_key(TerminalInput *tinput)
{
	int32 c;

	c = read_key();
	if (IMPLEMENTED(c)) {
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
			TerminalInput_eol(tinput);
			break;
		case HOME_KEY:
			goto_home(tinput);
			break;
		case CTRL_KEY('l'):
			clear_screen();
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
		/// Unimplemented
		case CTRL_KEY('h'):
		case CTRL_KEY('x'):
		case CTRL_KEY('j'):
		case CTRL_KEY('k'):
		case CTRL_KEY('i'):
			break;
		default:
			TerminalInput_insert(tinput, c);
			break;
		}
	}
	return 1;
}

void inbuff_goto_end(TerminalInput *buffer)
{
	while (inbuff_cursor_down(buffer))
		;
	TerminalInput_eol(buffer);
}

void read_input(TerminalInput *buffer)
{
	/* Set reading flag in case we get interrupted */
	terminal.tm_reading = 1;
	fflush(stderr);
	/* Set raw mode */
	set_terminal_mode(&terminal.tm_rawterm);
	/* Get terminal size */
	get_winsize_or_panic(&terminal.tm_rows, &terminal.tm_columns);
	/* Get terminal cursor position (column) */
	get_cursor_pos(NULL, &terminal.tm_col);
	/* Loop until newline */
	while (inbuff_process_key(buffer)) {
		unblock_signals(); /* Unblock signals while waiting for input */
	}
	/* Null-Terminate the buffer */
	buffer->in_ibf[buffer->in_len++] = '\0';
	inbuff_goto_end(buffer);
	fprintf(stderr, "\r\n");
	fflush(stderr);
	/* Unset the reading flag */
	terminal.tm_reading = 0;
	/* Set default shell terminal mode */
	set_terminal_mode(&terminal.tm_dflterm);
	/* Unblock signals */
	unblock_signals();
}
