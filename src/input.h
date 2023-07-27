#ifndef __ASH_INPUT_H__
#define __ASH_INPUT_H__

#include "ashe_string.h"

#include <stdatomic.h>
#include <termios.h>

#define INSIZE 200

#define MAXLINE ARG_MAX

#define FFLUSH(stream, block)                                                  \
  do {                                                                         \
    block;                                                                     \
    fflush((stream));                                                          \
  } while (0)
/// ANSI Escape sequences
#define yellow(text) "\x1B[33m" text "\x1B[0m"
#define green(text) "\x1B[32m" text "\x1B[0m"
#define red(text) "\x1B[31m" text "\x1B[0m"
#define bred(text) "\x1B[91m" text "\x1B[0m"
#define cyan(text) "\x1B[36m" text "\x1B[0m"
#define blue(text) "\x1B[34m" text "\x1B[0m"
#define bblue(text) "\x1B[94m" text "\x1B[0m"
#define bwhite(text) "\x1B[97m" text "\x1B[0m"
#define byellow(text) "\x1B[93m" text "\x1B[0m"
/// Cursor movement
#define mv_cur_left(stream) FFLUSH(stream, fprintf((stream), "\033[1D"))
#define mv_cur_right(stream) FFLUSH(stream, fprintf((stream), "\033[1C"))
#define mv_cur_up(stream) FFLUSH(stream, fprintf((stream), "\033[1A"))
#define mv_cur_down(stream) FFLUSH(stream, fprintf((stream), "\033[1B"))
#define mv_cur_col(stream, col)                                                \
  FFLUSH(stream, fprintf((stream), "\033[%ldG", col))
#define sv_cur_pos(stream) FFLUSH(stream, fprintf((stream), "\033[s"))
#define ld_cur_pos(stream) FFLUSH(stream, fprintf((stream), "\033[u"))
#define req_cur_pos(stream) FFLUSH(stream, fprintf((stream), "\033[6n"))
/// Delete char at cursor
#define del_char(stream) FFLUSH(stream, fprintf((stream), "\b \b"))

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
void inbuff_print(inbuff_t *buffer);
void init_rawterm(void);
void init_dflterm(void);
void set_dflmode(void);
void set_rawtmode(void);

#endif
