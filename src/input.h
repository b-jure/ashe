#ifndef __ASH_INPUT_H__
#define __ASH_INPUT_H__

#include "ashe_string.h"

#include <termios.h>

#define INSIZE 200

typedef struct inbuff_t inbuff_t;

extern struct termios shell_tmodes;

int read_input(string_t *buffer);

#endif
