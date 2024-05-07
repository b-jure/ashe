/* ----------------------------------------------------------------------------------------------
 * Copyright (C) 2023-2024 Jure Bagić
 *
 * This file is part of ashe.
 * ashe is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ashe is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ashe.
 * If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------------------------*/

#ifndef APARSER_H
#define APARSER_H

#include "acommon.h"
#include "aarray.h"
#include "alex.h"
#include "atoken.h"

#define BM_STRING    (BM(TK_WORD) | BM(TK_KVPAIR) | BM(TK_NUMBER))
#define BM_SEPARATOR (BM(TK_AND) | BM(TK_SEMICOLON))

#define ARGV(cmd, i) (*a_arr_ccharp_index(&(cmd)->sc_argv, i))
#define ARGC(cmd)    a_arr_len((cmd)->sc_argv)

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
	a_ssize rd_lhsfd; /* lhs file descriptor */
	a_ssize rd_rhsfd; /* rhs file descriptor */
	const char *rd_fname; /* filepath */
	enum a_redirect_op rd_op; /* redirection op */
	volatile a_byte rd_append; /* append flag */
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
	a_ubyte pl_bg; /* run in background */
	const char *pl_input; /* debug */
};

ARRAY_NEW(a_arr_pipeline, struct a_pipeline)

struct a_list {
	a_arr_pipeline ls_pipes;
};

ARRAY_NEW(a_arr_list, struct a_list)

struct a_block {
	a_arr_list bl_lists;
	a_memmax bl_subst; /* recursion depth '()' */
};

void a_block_init(struct a_block *block);
void a_block_free(struct a_block *block);
a_int32 ashe_parse(const char *cstr);

#endif
