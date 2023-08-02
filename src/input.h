#ifndef __ASH_INPUT_H__
#define __ASH_INPUT_H__

#include "ashe_utils.h"
#include "vec.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <termios.h>

#define INSIZE 200
#define MAXLINE ARG_MAX
#define MAXNAME 1024 /* Maximum size of username */

/// ANSI CSI - Control Sequence Introducer
#define ESC(seq) "\033[" #seq

#define FFLUSH(block)                                                          \
  do {                                                                         \
    block;                                                                     \
    fflush(stderr);                                                            \
    if (__glibc_unlikely(ferror(stderr) != 0))                                 \
      die();                                                                   \
  } while (0)

#define write_or_die(ptr, n)                                                   \
  do {                                                                         \
    if (write(STDERR_FILENO, ptr, n) == -1)                                    \
      die();                                                                   \
    fflush(stderr);                                                            \
  } while (0)

#define get_size_or_die(row, col)                                              \
  do {                                                                         \
    if (__glibc_unlikely(get_window_size((row), (col)) == FAILURE &&           \
                         get_window_size_fallback((row), (col)) == FAILURE))   \
      die();                                                                   \
  } while (0)

/// Cursor movement
#define mv_cur_home ESC(H)
#define mv_cur_left(n) ESC(n) "D"
#define mv_cur_right(n) ESC(n) "C"
#define mv_cur_up(n) ESC(n) "A"
#define mv_cur_down(n) ESC(n) "B"
#define mv_cur_col(col) ESC(col) "G"
#define sv_cur_pos ESC(s)
#define ld_cur_pos ESC(u)
#define req_cur_pos ESC(6n)
#define hide_cur ESC(?25l)
#define show_cur ESC(?25h)
/// Erase functions
#define clrscr_down ESC(0J)
#define clrscr_up ESC(1J)
#define clrscr ESC(2J)
#define erase_cur_to_eol ESC(K)
#define erase_sol_to_cur ESC(1K)
#define erase_line(n) ESC(n) "K"
/// Delete char at cursor
#define del_char "\b \b"

/// Modes
#define bold(text) ESC(1m) text ESC(22m)
#define italic(text) ESC(3m) text ESC(23m)
/// Colors
#define magenta(text) ESC(35m) text ESC(0m)
#define yellow(text) ESC(33m) text ESC(0m)
#define byellow(text) ESC(93m) text ESC(0m)
#define green(text) ESC(32m) text ESC(0m)
#define red(text) ESC(31m) text ESC(0m)
#define bred(text) ESC(91m) text ESC(0m)
#define cyan(text) ESC(36m) text ESC(0m)
#define blue(text) ESC(34m) text ESC(0m)
#define bblue(text) ESC(94m) text ESC(0m)
#define bwhite(text) ESC(97m) text ESC(0m)
#define byellow(text) ESC(93m) text ESC(0m)
/// Custom formatting
#define obrack cyan("[")
#define cbrack cyan("]")
#define bracketed(text) obrack text cbrack

/// Shell prefix format
#define ASHE_PREFIX bold(bred("ashe"))
/// Shell warnings prefix format
#define ASHE_WARN_PREFIX                                                       \
  obrack ASHE_PREFIX bold(cyan(" ~ ")) italic(byellow("warning"))              \
      cbrack cyan(":>")
/// Shell errors prefix format
#define ASHE_ERR_PREFIX                                                        \
  obrack ASHE_PREFIX bold(cyan(" ~ ")) italic(bred("error")) cbrack cyan(":>")

typedef struct {
  uint16_t cr_col;
  uint16_t cr_row;
} cursor_t;

/// Terminal input buffer
typedef struct {
  byte in_buffer[MAXLINE];
  vec_t *in_rows; /* Buffer rows as slices of bytes */
  size_t in_len;
  cursor_t in_bcur; /* Buffer cursor */
  cursor_t in_tcur; /* Terminal cursor */
} inbuff_t;

typedef struct {
  bool tm_reading;           /* User input reprint flag */
  struct termios tm_dflterm; /* Default terminal modes */
  struct termios tm_rawterm; /* Raw (input reading) terminal modes */
  inbuff_t tm_inbuff;        /* User input/state storage */
  uint16_t rows;
  uint16_t columns;
} terminal_t;

#define inbuff_clear(inbuff)                                                   \
  do {                                                                         \
    (inbuff)->len = 0;                                                         \
    (inbuff)->curp = 0;                                                        \
  } while (0)

void read_input(inbuff_t *buffer);
void inbuff_print(inbuff_t *buffer, bool interrupted);
void init_rawterm(struct termios *rawterm);
void init_dflterm(struct termios *dflterm);
void settmode(struct termios *tmode);
void pprompt(void);
int get_window_size(uint16_t *height, uint16_t *width);
int get_window_size_fallback(uint16_t *height, uint16_t *width);
void terminal_init(terminal_t *term);

#endif
