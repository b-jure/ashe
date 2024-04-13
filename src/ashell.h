#ifndef ASHELL_H
#define ASHELL_H

#include "aparser.h"
#include "acommon.h"
#include "ainput.h"
#include "ajobcntl.h"

#include <signal.h>
#include <setjmp.h>

struct a_jmpbuf {
	jmp_buf buf_jmpbuf;
	volatile a_int32 buf_code;
};

enum a_setting_type {
	ASETTING_NOCLOBBER = (1 << 0),
};

struct a_settings {
	a_ubyte sett_noclobber : 1; /* do not overwrite existing file */
}; /* shell settings */

struct a_flags {
	volatile a_ubyte exit : 1; /* set if already warned before exiting or in fork */
	volatile a_ubyte isfork : 1; /* set if this is a forked shell process */
	volatile a_ubyte interactive : 1; /* set if shell is interactive */
	volatile a_ubyte panic : 1; /* set if panic was triggered */
};

struct a_shell {
	struct a_jobcntl sh_jobcntl;
	struct a_term sh_term;
	struct a_lexer sh_lexer;
	a_arr_ccharp sh_buffers;
	a_arr_char sh_prompt;
	a_arr_char sh_welcome;
	a_arr_char sh_status;
	struct a_block sh_block;
	struct a_jmpbuf sh_buf;
	struct a_flags sh_flags;
	struct a_settings sh_settings;
	volatile sig_atomic_t sh_int; /* set if we got interrupted */
	a_ubyte sh_dirtyfd[3]; /* fd flags */
};

extern struct a_shell ashe; /* global */

void a_shell_init(struct a_shell *sh);
void a_shell_free(struct a_shell *sh);
void ashe_free_ccharp(void *ptr);

#endif
