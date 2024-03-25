#ifndef APARSER_H
#define APARSER_H

#include "acommon.h"
#include "aarray.h"
#include "alex.h"
#include "abuiltin.h"
#include "atoken.h"

typedef enum {
	OP_REDIRECT_ERROUT = 0, /* '>&'string | '&>'string | '&>>'string */
	OP_REDIRECT_OUT, /* [n]'>'string | [n]'>>'string */
	OP_REDIRECT_IN, /* [n]'<'string */
	OP_REDIRECT_INOUT, /* [n]'<>'string */
	OP_DUP_IN, /* [n]'<&'n */
	OP_DUP_OUT, /* [n]'>&'n */
	OP_CLOSE, /* [n]'>&-' | [n]'<&-' */
} Operation;

typedef struct {
	const char *filepath; /* redirection filepath */
	ssize fd; /* file descriptor */
	ssize fddup; /* file descriptor to duplicate */
	ubyte append; /* open file with append flag */
	Operation op;
} FileHandle;

ARRAY_NEW(ArrayFileHandle, FileHandle)

struct Command {
	ArrayCharptr argv; /* command name and arguments */
	ArrayCharptr env; /* environmental variables */
	ArrayFileHandle fhandles; /* file handles */
	uint32 cmd_settings; /* bitmask of settings [@builtin.h] */
	ubyte pipand; /* is this '|&' pipeline */
};

ARRAY_NEW(ArrayCommand, Command)

#define ARGV(cmd, n) (cmd)->argv.data[n]
#define ENV(cmd, n)  (cmd)->env.data[n]

typedef struct {
	ArrayCommand commands; /* commands connected with '|' or '|&' */
	Connection connection; /* '&&', '||' or none */
} Pipeline;

ARRAY_NEW(ArrayPipeline, Pipeline)

typedef struct {
	ArrayPipeline pipelines; /* collection of pipelines */
	ubyte is_background; /* is process being run in background ? */
} Conditional;

ARRAY_NEW(ArrayConditional, Conditional)

void Conditional_free(Conditional *cond);
int32 parse(const char *cstr);

#endif
