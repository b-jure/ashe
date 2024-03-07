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
    Buffer file;       // filepath or empty if fd_right is present
    int32 fd_left;     // left side file descriptor
    int32 fd_right;    // right side file descriptor
    ubyte write  : 1;  // if flag set open file to write '>'
    ubyte append : 1;  // if flag set open file with append flag
    ubyte close  : 1;  // if flag set then close the fd_left
} FDContext;           // File descriptor context

/* Array of 'Redirection's */
ARRAY_NEW(ArrayFDContext, FDContext);

typedef struct {
    ArrayBuffer argv;      /* command name and arguments */
    ArrayBuffer env;       /* environmental variables */
    ArrayFDContext fdcs;   /* file descriptor context array */
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
