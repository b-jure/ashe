#ifndef APARSER_H
#define APARSER_H

#include "acommon.h"
#include "aarray.h"
#include "abuiltin.h"
#include "alex.h"
#include "atoken.h"


ARRAY_NEW(ArrayCharptr, char*);


typedef enum {
    OP_REDIRECT_ERROUT = 0,   /* '>&'string | '&>'string | '&>>'string  */
    OP_REDIRECT_OUT,          /* [n]'>'string | [n]'>>'string           */
    OP_REDIRECT_IN,           /* [n]'<'string                           */
    OP_REDIRECT_INOUT,        /* [n]'<>'string                          */
    OP_DUP_IN,                /* [n]'<&'n                               */
    OP_DUP_OUT,               /* [n]'>&'n                               */
    OP_CLOSE_N,               /* [n]'>&-' | [n]'<&-'                    */
} Operation;

typedef struct {
    union {
        struct {
            const char* filepath;  // redirection filepath
            memmax fd;             // file descriptor
            ubyte append;          // open file with append flag
        } s1; // used when redirecting to a file
        struct {
            memmax fd;          // file descriptor
            memmax fddup;       // file descriptor to duplicate
            const char* fdstr;  // special files (stdin...)
        } s2; // used when duplicating file descriptors
    } u;
    Operation op;
} FileHandle;

ARRAY_NEW(ArrayFileHandle, FileHandle);

#define fhredirect(fh) ((fh)->u.s1)
#define fhdup(fh) ((fh)->u.s2)




typedef struct {
    ArrayCharptr argv;          /* command name and arguments */
    ArrayCharptr env;           /* environmental variables */
    ArrayFileHandle fhandles;   /* file handles */
    uint32 cmd_settings;        /* bitmask of settings [@builtin.h] */
    ubyte stderr_pip;           /* is this '|&' pipeline */
} Command;

ARRAY_NEW(ArrayCommand, Command);

#define ARGV(cmd, n) ArrayBuffer_index(&(cmd)->argv, n)->data
#define ENV(cmd, n) ArrayBuffer_index(&(cmd)->env, n)->data




typedef struct {
    ArrayCommand commands; /* commands connected with '|' or '|&' */
    Connection connection; /* '&&', '||' or none */
} Pipeline;

ARRAY_NEW(ArrayPipeline, Pipeline);




typedef struct {
    ArrayPipeline pipelines;    /* collection of pipelines */
    ubyte is_background;        /* is process being run in background ? */
} Conditional;

ARRAY_NEW(ArrayConditional, Conditional);

void Conditional_free(Conditional* cond);
int32 parse(const char* cstr);

#endif
