#ifndef ASHELL_H
#define ASHELL_H

#include "aparser.h"
#include "acommon.h"
#include "ainput.h"
#include "ajobcntl.h"

#include <signal.h>
#include <setjmp.h>

typedef struct {
	jmp_buf buf_jmpbuf;
	volatile int32 buf_code;
} AsheJmpBuf;

typedef enum {
	SETTING_EXIT = (1 << 0),
	SETTING_NOCLOBBER = (1 << 1),
} Setting;

typedef struct {
	ubyte sett_exit : 1; /* if set exit after executing next command */
	ubyte sett_noclobber : 1; /* if set do not overwrite existing file */
	// TODO: Implement more settings...
} Settings; /* shell settings */

typedef struct {
	volatile ubyte exit : 1; /* set if last command was 'exit' */
	volatile ubyte isfork : 1; /* set if this is forked shell process */
	volatile ubyte interactive : 1; /* set if shell is interactive */
} Flags;

typedef struct {
	JobControl sh_jobcntl;
	Terminal sh_term;
	Lexer sh_lexer;
	ArrayCharptr sh_buffers;
	Buffer sh_prompt;
	Buffer sh_welcome;
	ArrayConditional sh_conds;
	AsheJmpBuf sh_buf;
	Settings sh_settings;
	volatile sig_atomic_t sh_int; /* set if we got interrupted */
	Flags sh_flags;
} Shell;

extern Shell ashe; /* global */

void Shell_init(Shell *sh);
void wafree_charp(void *ptr);
void Shell_free(Shell *sh);

#endif
