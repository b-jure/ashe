#ifndef APARSER_H
#define APARSER_H

#include "acommon.h"
#include "aarray.h"
#include "alex.h"
#include "atoken.h"

#define BM_STRING (BM(TK_WORD) | BM(TK_KVPAIR) | BM(TK_NUMBER))

ARRAY_NEW(a_arr_ccharp, const char *)

enum a_cmdtype {
	ACMD_SIMPLE = 0,
};

enum a_connect {
	ACON_NONE = 1,
	ACON_AND = 2,
	ACON_OR = 4,
};

enum a_redirect_op {
	ARDOP_REDIRECT_ERROUT = 0,
	ARDOP_REDIRECT_OUT,
	ARDOP_REDIRECT_CLOB,
	ARDOP_REDIRECT_IN,
	ARDOP_REDIRECT_INOUT,
	ARDOP_DUP_IN,
	ARDOP_DUP_OUT,
	ARDOP_CLOSE,
};

struct a_redirect {
	ssize rd_lhsfd; /* lhs file descriptor */
	ssize rd_rhsfd; /* rhs file descriptor */
	const char *rd_fname; /* filepath */
	enum a_redirect_op rd_op; /* redirection op */
	byte rd_append; /* append flag */
};

ARRAY_NEW(a_arr_redirect, struct a_redirect)

struct a_simple_cmd { /* simple command */
	a_arr_ccharp sc_argv;
	a_arr_ccharp sc_env;
	a_arr_redirect sc_rds;
};

struct a_cmd { /* tagged union */
	enum a_cmdtype c_type;
	union {
		struct a_simple_cmd scmd;
	} c_u;
};

ARRAY_NEW(a_arr_cmd, struct a_cmd)

struct a_pipeline {
	a_arr_cmd pl_cmds; /* commands */
	enum a_connect pl_con; /* connection type */
	ubyte pl_bg; /* run asynchronously */
	const char *pl_input; /* debug */
};

ARRAY_NEW(a_arr_pipeline, struct a_pipeline)

struct a_list {
	a_arr_pipeline ls_pipes;
};

ARRAY_NEW(a_arr_list, struct a_list)

struct a_block {
	a_arr_list bl_lists;
	memmax bl_subst; /* recursion depth '()' */
};

void a_block_free(struct a_block *block);
int32 a_parse_block(const char *cstr);

#endif
