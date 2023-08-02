#ifndef __ASH_SHELL_H__
#define __ASH_SHELL_H__

#include "cmdline.h"
#include "input.h"
#include "jobctl.h"

#include <termios.h>

typedef struct {
  bool sh_intr;             /* Flag indicating interrupt occured */
  commandline_t sh_cmdline; /* Parser storage */
  joblist_t sh_jlist;       /* Shell joblist */
  terminal_t sh_term;       /* Shell terminal settings and raw input buffer */
} shell_t;

extern shell_t shell; /* Shell */

void shell_init(void);

#endif
