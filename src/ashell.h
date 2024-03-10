#ifndef ASHELL_H
#define ASHELL_H

#include "aarray.h"
#include "aparser.h"
#include "acommon.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "alex.h"

#include <setjmp.h>




typedef struct {
    jmp_buf buf_jmpbuf;
    volatile int32 buf_code;
} AsheJmpBuf;




typedef enum {
    SETTING_EXIT      = (1 << 0),
    SETTING_NOCLOBBER = (1 << 1),
} Setting;

typedef struct {
    ubyte sett_exit      : 1;  /* if set exit after executing next command */
    ubyte sett_noclobber : 1;  /* if set do not overwrite existing file */
    // TODO: Implement more settings...
} Settings; /* shell settings */




/* Note: 
 * sh_buffers index 0 (first value) -> welcome message cstring
 * sh_buffers index 1 (second value) -> prompt cstring
 * sh_buffers index >=2 (other values) -> cstring tokens */
typedef struct {
    JobControl sh_jobctl;
    Terminal sh_term;
    Lexer sh_lexer;
    ArrayCharptr sh_buffers;
    ArrayConditional sh_conds;
    AsheJmpBuf sh_buf;
    Settings sh_settings;
    volatile ubyte sh_int; /* set if we got interrupted */
} Shell;

extern Shell ashe;  /* global */


void Shell_init(Shell* shell);

#endif
