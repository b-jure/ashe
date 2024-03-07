#ifndef APARSER_H
#define APARSER_H

#include "acommon.h"
#include "aarray.h"
#include "lexer.h"
#include "shell.h"
#include "token.h"




/* Array of 'Buffer's */
ARRAY_NEW(ArrayBuffer, Buffer);

typedef struct {
    int32 fd_left;
    int32 fd_right;
    ubyte read;
    ubyte append;
    Buffer file;
} Redirection;

/* Array of 'Redirection's */
ARRAY_NEW(ArrayRedirection, Redirection);

typedef struct {
    ArrayBuffer argv; /* command name and arguments */
    ArrayBuffer env; /* environmental variables */
    ArrayRedirection rds; /* redirections */
} Command;





/* Array of 'Command's */
ARRAY_NEW(ArrayCommand, Command);
typedef struct {
    ArrayCommand commands;
    Connection connection; /* '&&', '||' or none */
} Pipeline;





/* Array of 'Pipeline's */
ARRAY_NEW(ArrayPipeline, Pipeline);
typedef struct {
    ArrayPipeline pipelines; /* collection of pipelines */
    ubyte is_background; /* is process being run in background ? */
} Conditional;




/* Array of 'Conditional's */
ARRAY_NEW(ArrayConditional, Conditional);

/* Parse command line and store AST as array of 'Conditional's. */
int32 parse(ArrayConditional* conds);


#endif
