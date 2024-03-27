#ifndef ADBG_H
#define ADBG_H

#include <stdio.h>

#include "acommon.h"

void debug_cursor(void);
void debug_lines(void);
void remove_logfiles(void); /* atexit() in 'ashell.c' */
void logfile_create(const char *logfile, int32 which);

#define ALOG_CURSOR 0
#define ALOG_LINES  1
extern const char *logfiles[];

#endif
