#ifndef __ASH_CMDLINE_H__
#define __ASH_CMDLINE_H__

#include "vec.h"

typedef struct {
  vec_t *conditionals;
} commandline_t;

commandline_t commandline_new(void);
void commandline_execute(commandline_t *cmdline, int *status);
void commandline_drop(commandline_t *cmdline);

#endif
