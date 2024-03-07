#ifndef ASHELL_H
#define ASHELL_H

#include "aarray.h"
#include "acommon.h"
#include "input.h"
#include "jobctl.h"
#include "lexer.h"

#include <setjmp.h>


/* jmp_buf wrapper */
typedef struct {
    jmp_buf buf;
    volatile int32 res;
} AsheJmpBuf;


ARRAY_NEW(ArrayCharptr, char*);

/**
 * Note: 
 * ArrayCharptr index 0 (first value) -> welcome message buffer
 * ArrayCharptr index 1 (second value) -> prompt buffer
 **/
typedef struct {
    AsheJmpBuf sh_buf;
    Joblist sh_jlist;
    Terminal sh_term;
    Lexer sh_lexer;
    /* Storage for static and input buffers, used only
     * to free the buffer storage on shell cleanup or
     * after the parsed input gets executed (tokens). */
    ArrayCharptr sh_buffers; 
    ubyte sh_int; /* Flag indicating if interrupt occurred */
    ubyte sh_exit; /* Flag indicating if user tried exiting while background jobs are running */
} Shell;

extern Shell ashe; /* Shell global */

#define SHELL_BUFFER(shell) ((shell)->sh_term.tm_input.in_buffer)

void Shell_init(Shell* shell);

#endif
