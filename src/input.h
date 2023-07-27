#ifndef __ASH_INPUT_H__
#define __ASH_INPUT_H__

#include "ashe_utils.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <termios.h>

#define INSIZE 200
#define MAXLINE ARG_MAX
#define MAXNAME 1024 /* Maximum size of username */

/// ANSI CSI - Control Sequence Introducer
#define ESC(seq) "\033[" seq
/// Wrapper for flushing 'stdout'
#define FFLUSH(block)                                                          \
  do {                                                                         \
    block;                                                                     \
    fflush(NULL);                                                              \
  } while (0)
/// Cursor movement
#define mv_cur_left FFLUSH(fprintf(stderr, ESC("1D")))
#define mv_cur_right FFLUSH(fprintf(stderr, ESC("1C")))
#define mv_cur_up FFLUSH(fprintf(stderr, ESC("1A")))
#define mv_cur_down FFLUSH(fprintf(stderr, ESC("1B")))
#define mv_cur_col(col) FFLUSH(fprintf(stderr, ESC("%ldG"), (size_t)col))
#define sv_cur_pos FFLUSH(fprintf(stderr, ESC("s")))
#define ld_cur_pos FFLUSH(fprintf(stderr, ESC("u")))
#define req_cur_pos FFLUSH(fprintf(stderr, ESC("6n")))
#define hide_cur FFLUSH(fprintf(stderr, ESC("?25l")))
#define show_cur FFLUSH(fprintf(stderr, ESC("?25h")))
/// Delete char at cursor
#define del_char FFLUSH(fprintf(stderr, "\b \b"))

/// Modes
#define bold(text) ESC("1m") text ESC("22m")
#define italic(text) ESC("3m") text ESC("23m")
/// Colors
#define magenta(text) ESC("35m") text ESC("0m")
#define yellow(text) ESC("33m") text ESC("0m")
#define byellow(text) ESC("93m") text ESC("0m")
#define green(text) ESC("32m") text ESC("0m")
#define red(text) ESC("31m") text ESC("0m")
#define bred(text) ESC("91m") text ESC("0m")
#define cyan(text) ESC("36m") text ESC("0m")
#define blue(text) ESC("34m") text ESC("0m")
#define bblue(text) ESC("94m") text ESC("0m")
#define bwhite(text) ESC("97m") text ESC("0m")
#define byellow(text) ESC("93m") text ESC("0m")
/// Custom...
#define obrack cyan("[")
#define cbrack cyan("]")

/// Shell prefix format
#define ASHE_PREFIX bold(bred("ashe"))
/// Shell warnings prefix format
#define ASHE_WARN_PREFIX                                                       \
  obrack ASHE_PREFIX bold(cyan(" ~ ")) italic(byellow("warning"))              \
      cbrack cyan(":>")
/// Shell errors prefix format
#define ASHE_ERR_PREFIX                                                        \
  obrack ASHE_PREFIX bold(cyan(" ~ ")) italic(bred("error")) cbrack cyan(":>")

/// Terminal input buffer
typedef struct {
  byte buffer[MAXLINE];
  size_t len;
  size_t curp;
  size_t cur_col;
} inbuff_t;

extern inbuff_t terminal_input;
extern struct termios dflterm;
extern struct termios rawterm;

#define inbuff_clear(inbuff)                                                   \
  (inbuff)->len = 0;                                                           \
  (inbuff)->curp = 0

int read_input(inbuff_t *buffer);
void inbuff_print(inbuff_t *buffer, bool interrupted);
void init_rawterm(void);
void init_dflterm(void);
void set_dflmode(void);
void set_rawtmode(void);
void pprompt(void);

#endif
