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

#include "aarray.h"
#include "acommon.h"
#include "adbg.h"
#include "autils.h"
#include "alex.h"
#include "ashell.h"
#include "aparser.h"
#include "atoken.h"

#include <fcntl.h>
#include <setjmp.h>

/* Jump out of code into 'AsheJmpBuf'.
 * Additionally set the buffer res to 'code'. */
#define jump_out(code)                              \
	do {                                        \
		ashe.sh_buf.buf_code = code;        \
		longjmp(ashe.sh_buf.buf_jmpbuf, 1); \
	} while (0)

/* Bit mask from 'Tokentype' */
#define BM(type) (1 << (type))

/* Parsing errors */
#define ERR_EXPECT   0
#define ERR_CMDSUBST 1
static const char *perrors[] = {
	"expected %s, instead got '%s'.",
	"can't have command substitution here.",
};

static const a_ubyte is_redirection[TK_NUMBER + 1] = {
	0, 0, 1, /* TK_LESS_AND '<&' */
	1, /* TK_GREATER_AND '>&' */
	1, /* TK_GREATER_PIPE '>|' */
	1, /* TK_GREATER_GREATER '>>' */
	1, /* TK_AND_GREATER '&>' */
	1, /* TK_AND_GREATER_GREATER '&>>' */
	1, /* TK_LESS_GREATER '<>' */
	1, /* TK_LESS '<' */
	1, /* TK_GREATER '>' */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* Advance to the next token. */
ASHE_PRIVATE void nexttok(struct a_lexer *restrict lexer)
{
	lexer->prev = lexer->curr;
	lexer->curr = a_lexer_next(lexer);
	if (ASHE_UNLIKELY(A_CTOK.type == TK_ERROR)) { /* lex error ? */
		ashe_eprintf(A_CTOK.u.error);
		jump_out(-1);
	}
#if defined(ASHE_DBG_LEX) && defined(ASHE_DBG)
	debug_current_token(&A_CTOK);
#endif
}

ASHE_PRIVATE a_ubyte match(a_memmax bitmask)
{
	if (BM(A_CTOK.type) & bitmask) {
		nexttok(&A_LEX);
		return 1;
	}
	return 0;
}

ASHE_PRIVATE void expect_error(const char *restrict what)
{
	const char *invalid;

	invalid = a_token_str(&A_CTOK);
	ashe_eprintf(perrors[ERR_EXPECT], what, invalid);
	jump_out(-1);
}

ASHE_PRIVATE inline void expect(a_ubyte next, a_memmax bitmask, const char *restrict type)
{
	if (next)
		nexttok(&A_LEX);
	if (BM(A_CTOK.type) & bitmask)
		return;
	expect_error(type);
}

ASHE_PRIVATE inline void a_redirect_init(struct a_redirect *restrict rd)
{
	rd->rd_lhsfd = -1;
	rd->rd_rhsfd = -1;
	rd->rd_append = 0;
	rd->rd_op = 0;
}

ASHE_PRIVATE inline void a_simple_cmd_init(struct a_simple_cmd *restrict scmd)
{
	a_arr_ccharp_init(&scmd->sc_argv);
	a_arr_ccharp_init(&scmd->sc_env);
	a_arr_redirect_init(&scmd->sc_rds);
}

ASHE_PRIVATE inline void a_simple_cmd_free(struct a_simple_cmd *restrict scmd)
{
	a_arr_ccharp_free(&scmd->sc_argv, NULL);
	a_arr_ccharp_free(&scmd->sc_env, NULL);
	a_arr_redirect_free(&scmd->sc_rds, NULL);
}

ASHE_PRIVATE inline void a_cmd_free(struct a_cmd *restrict cmd)
{
	switch (cmd->c_type) {
	case ACMD_SIMPLE:
		a_simple_cmd_free(&cmd->c_u.scmd);
		break;
	default:
		/* UNREACHED */
		ashe_assert(0);
	}
}

ASHE_PRIVATE inline void a_pipeline_init(struct a_pipeline *restrict pipeline)
{
	a_arr_cmd_init(&pipeline->pl_cmds);
	pipeline->pl_con = ACON_NONE;
	pipeline->pl_bg = 0;
	pipeline->pl_input = NULL;
}

ASHE_PRIVATE inline void a_pipeline_free(struct a_pipeline *restrict pipeline)
{
	a_arr_cmd_free(&pipeline->pl_cmds, (FreeFn)a_cmd_free);
}

ASHE_PRIVATE inline void a_list_init(struct a_list *restrict list)
{
	a_arr_pipeline_init(&list->ls_pipes);
}

ASHE_PRIVATE inline void a_list_free(struct a_list *restrict list)
{
	a_arr_pipeline_free(&list->ls_pipes, (FreeFn)a_pipeline_free);
}

ASHE_PUBLIC void a_block_init(struct a_block *restrict block)
{
	a_arr_list_init(&block->bl_lists);
	block->bl_subst = 0;
}

ASHE_PUBLIC void a_block_free(struct a_block *restrict block)
{
	a_arr_list_free(&block->bl_lists, (FreeFn)a_list_free);
}

/*
 *
 *			PARSING
 *
 */

/* Helper */
ASHE_PRIVATE inline const char *getfilename(void)
{
	if (A_CTOK.type == TK_NUMBER)
		return *a_arr_ccharp_last(&ashe.sh_strings);
	return A_CTOK_STR();
}

/*
 * [SYNTAX]
 * redirect_in ::= '<' filename
 */
ASHE_PRIVATE void redirect_in(struct a_redirect *restrict rdp)
{
	if (rdp->rd_lhsfd == -1)
		rdp->rd_lhsfd = 0;
	expect(1, BM_STRING, "filename (string)");
	rdp->rd_fname = getfilename();
	rdp->rd_op = ARDOP_REDIRECT_IN;
}

/*
 * [SYNTAX]
 * redirect_out ::= '>' filename
 *		  | '>>' filename
 *		  | '>|' filename
 */
ASHE_PRIVATE void redirect_out(struct a_redirect *restrict rdp)
{
	if (rdp->rd_lhsfd == -1)
		rdp->rd_lhsfd = 1;
	expect(1, BM_STRING, "filename (string)");
	rdp->rd_fname = getfilename();
	if (rdp->rd_op != ARDOP_REDIRECT_CLOB)
		rdp->rd_op = ARDOP_REDIRECT_OUT;
}

/*
 * [SYNTAX]
 * redirect_outerr ::= '>&' filename
 *		     | '&>' filename
 */
ASHE_PRIVATE void redirect_outerr(struct a_redirect *restrict rdp, a_ubyte skipped)
{
	rdp->rd_op = ARDOP_REDIRECT_ERROUT;
	if (!skipped)
		nexttok(&A_LEX);
	expect(0, BM_STRING, "filename (string)");
	rdp->rd_fname = getfilename();
}

/*
 * [SYNTAX]
 * dupin_or_close ::= '>&' '-'
 * 		    | '>&' NUMBER
 */
ASHE_PRIVATE a_ubyte is_dupout_or_close(struct a_redirect *restrict rdp)
{
	if (A_PTOK.type == TK_NUMBER)
		rdp->rd_lhsfd = A_PTOK_NUM();
	nexttok(&A_LEX);
	if (A_CTOK.type == TK_NUMBER) {
		rdp->rd_rhsfd = A_CTOK_NUM();
		rdp->rd_op = ARDOP_DUP_OUT;
		return 1;
	} else if (A_CTOK.type == TK_MINUS) {
		rdp->rd_op = ARDOP_CLOSE;
		return 1;
	}
	return 0;
}

/*
 * [SYNTAX]
 * dupin_or_close ::= '<&' '-'
 * 		    | '<&' NUMBER
 */
ASHE_PRIVATE void dupin_or_close(struct a_redirect *restrict rdp)
{
	expect(1, BM(TK_NUMBER) | BM(TK_MINUS), "file descriptor or '-'");
	if (A_CTOK.type == TK_NUMBER) {
		rdp->rd_rhsfd = A_CTOK_NUM();
		rdp->rd_op = ARDOP_DUP_IN;
	} else {
		rdp->rd_op = ARDOP_CLOSE;
	}
}

/*
 * [SYNTAX]
 * redirect_inout ::= '<>' filename
 */
ASHE_PRIVATE void redirect_inout(struct a_redirect *restrict rdp)
{
	if (rdp->rd_lhsfd == -1)
		rdp->rd_lhsfd = 0;
	rdp->rd_op = ARDOP_REDIRECT_INOUT;
	expect(1, BM_STRING, "filename (string)");
	rdp->rd_fname = getfilename();
}

/*
 * [SYNTAX]
 * redirection ::= redirect_in
 *		 | NUMBER redirect_in
 *		 | redirect_out
 *		 | NUMBER redirect_out
 *		 | redirect_outerr
 *		 | NUMBER redirect_outerr
 *		 | redirect_inout
 *		 | NUMBER redirect_inout
 *		 | dupin_or_close
 *		 | NUMBER dupin_or_close
 *		 | dupout_or_close
 *		 | NUMBER dupout_or_close
 */
ASHE_PRIVATE void redirection(struct a_simple_cmd *restrict scmd)
{
	struct a_redirect *rdp;
	a_ubyte skipped;

	skipped = 0;
	rdp = a_arr_redirect_last(&scmd->sc_rds);

	switch (A_CTOK.type) {
	case TK_LESS:
		redirect_in(rdp);
		break;
	case TK_GREATER_AND:
		if (rdp->rd_lhsfd < 0)
			rdp->rd_lhsfd = 1;
		if (is_dupout_or_close(rdp))
			break;
		skipped = 1;
		/* FALLTHRU */
	case TK_AND_GREATER:
		rdp->rd_append = -1;
		/* FALLTHRU */
	case TK_AND_GREATER_GREATER:
		rdp->rd_append++;
		redirect_outerr(rdp, skipped);
		break;
	case TK_GREATER_PIPE:
		rdp->rd_op = ARDOP_REDIRECT_CLOB;
		/* FALLTHRU */
	case TK_GREATER_GREATER:
		rdp->rd_append = 1;
		/* FALLTHRU */
	case TK_GREATER:
		redirect_out(rdp);
		break;
	case TK_LESS_AND:
		if (rdp->rd_lhsfd < 0)
			rdp->rd_lhsfd = 0;
		dupin_or_close(rdp);
		break;
	case TK_LESS_GREATER:
		redirect_inout(rdp);
		break;
	default:
		/* UNREACHED */
		ashe_assert(0);
		break;
	}
}

/*
 * [SYNTAX]
 * simple_cmd_prefix ::= KVPAIR
 *		       | simple_cmd_prefix KVPAIR
 *		       | redirection
 *		       | simple_cmd_prefix redirection
 */
ASHE_PRIVATE void simple_cmd_prefix(struct a_simple_cmd *restrict scmd)
{
	struct a_redirect rd;
	enum a_toktype type;
	const char *numstr;

	a_redirect_init(&rd);
	for (;; nexttok(&A_LEX)) {
		type = A_CTOK.type;

		switch (type) {
		case TK_KVPAIR:
			a_arr_ccharp_push(&scmd->sc_env, A_CTOK_STR());
			break;
		case TK_NUMBER:
			numstr = *a_arr_ccharp_last(&ashe.sh_strings);
			nexttok(&A_LEX);
			if (!is_redirection[A_CTOK.type]) {
				a_arr_ccharp_push(&scmd->sc_argv, numstr);
				return;
			}
			rd.rd_lhsfd = A_PTOK_NUM();
			goto pushrd;
		default:
			if (!is_redirection[type])
				return;
pushrd:
			a_arr_redirect_push(&scmd->sc_rds, rd);
			a_redirect_init(&rd);
			redirection(scmd);
			break;
		}
	}
}

/*
 * [SYNTAX]
 * simple_cmd_command ::= WORD
 *			| NUMBER
 */
ASHE_PRIVATE void simple_cmd_command(struct a_simple_cmd *restrict scmd)
{
	a_arr_ccharp_push(&scmd->sc_argv, *a_arr_ccharp_last(&ashe.sh_strings));
	nexttok(&A_LEX);
}

/* forward declare for 'block_subst()' */
ASHE_PRIVATE inline void plist(struct a_block *restrict block, struct a_list *list);

/*
 * [SYNTAX]
 * block_subst ::= '(' plist ')'
 */
ASHE_PRIVATE void block_subst(struct a_block *restrict block)
{
	struct a_list list;
	a_memmax insert;

	++block->bl_subst;
	insert = block->bl_lists.len - block->bl_subst;
	a_list_init(&list);
	a_arr_list_insert(&block->bl_lists, insert, list);
	plist(block, a_arr_list_index(&block->bl_lists, insert));
	expect(0, BM(TK_RPAREN), "')' (end of command substitution)");
	--block->bl_subst;
}

/*
 * [SYNTAX]
 * simple_cmd_suffix ::= redirection
 *		       | simple_cmd_suffix redirection
 *		       | WORD
 *		       | simple_cmd_suffix WORD
 *		       | block_subst
 *		       | simple_cmd_suffix block_subst
 */
ASHE_PRIVATE void simple_cmd_suffix(struct a_block *restrict block, struct a_simple_cmd *scmd)
{
	struct a_redirect rd;
	const char *numstr;
	enum a_toktype type;

	for (;;) {
		type = A_CTOK.type;
		a_redirect_init(&rd);

		switch (type) {
		case TK_LPAREN:
			block_subst(block);
			break;
		case TK_WORD:
		case TK_KVPAIR:
			a_arr_ccharp_push(&scmd->sc_argv, A_CTOK_STR());
			break;
		case TK_NUMBER:
			numstr = *a_arr_ccharp_last(&ashe.sh_strings);
			nexttok(&A_LEX);
			if (!is_redirection[A_CTOK.type]) {
				a_arr_ccharp_push(&scmd->sc_argv, numstr);
				continue;
			}
			rd.rd_lhsfd = A_PTOK_NUM();
			goto pushrd;
		default:
			if (!is_redirection[type])
				return;
pushrd:
			a_arr_redirect_push(&scmd->sc_rds, rd);
			redirection(scmd);
			break;
		}
		nexttok(&A_LEX);
	}
}

/*
 * [SYNTAX]
 * simple_cmd ::= simple_cmd_prefix
 *	        | simple_cmd_prefix simple_cmd_command
 *	        | simple_cmd_prefix simple_cmd_command simple_cmd_suffix
 *	        | simple_cmd_command
 *	        | simple_cmd_command simple_cmd_suffix
 */
ASHE_PRIVATE void simple_cmd(struct a_block *restrict block, struct a_simple_cmd *scmd)
{
	simple_cmd_prefix(scmd);
	if (A_CTOK.type == TK_WORD || A_PTOK.type == TK_NUMBER) {
		if (ARGC(scmd) == 0) {
			ashe_assert(A_PTOK.type != TK_NUMBER);
			simple_cmd_command(scmd);
		}
		if (A_CTOK.type != TK_EOL)
			simple_cmd_suffix(block, scmd);
	} else if (ASHE_UNLIKELY(a_arr_len(scmd->sc_env) == 0 && a_arr_len(scmd->sc_rds) == 0)) {
		/* this: 'input... ['|' | '&&' | '||'] EOL' */
		expect_error("string");
	}
}

/*
 * [SYNTAX]
 * command ::= simple_cmd
 */
ASHE_PRIVATE void command(struct a_block *restrict block, struct a_cmd *cmd)
{
	switch (A_CTOK.type) {
	default: /* for now only supports simple commands */
		cmd->c_type = ACMD_SIMPLE;
		a_simple_cmd_init(&cmd->c_u.scmd);
		simple_cmd(block, &cmd->c_u.scmd);
		break;
	}
}

/*
 * [SYNTAX]
 * pipe_seq ::= command
 *	      | command '|' pipe_seq
 */
ASHE_PRIVATE void pipe_seq(struct a_block *block, struct a_pipeline *pipeline)
{
	const char *temp, *end;
	struct a_cmd cmd;

	temp = A_CTOK.start;

	do {
		a_arr_cmd_push(&pipeline->pl_cmds, cmd);
		command(block, a_arr_cmd_last(&pipeline->pl_cmds));
	} while (match(BM(TK_PIPE)));

	end = A_PTOK.end;
	if (BM(A_CTOK.type) & BM_SEPARATOR) {
		pipeline->pl_bg = (A_CTOK.type == TK_AND);
		end = A_CTOK.end;
	}
	pipeline->pl_input = ashe_dupstrn(temp, (end - temp));
	a_arr_ccharp_push(&ashe.sh_strings, pipeline->pl_input);
}

/*
 * [SYNTAX]
 * plist ::= pipe_seq
 *	   | pipe_seq '&&' plist
 *	   | pipe_seq '||' plist
 */
ASHE_PRIVATE inline void plist(struct a_block *block, struct a_list *list)
{
	struct a_pipeline pipeline, *last;

	a_pipeline_init(&pipeline);
	do {
		a_arr_pipeline_push(&list->ls_pipes, pipeline);
		last = a_arr_pipeline_last(&list->ls_pipes);
		pipe_seq(block, last);
		if (BM(A_CTOK.type) & BM_SEPARATOR)
			break;
		if (match(BM(TK_AND_AND)))
			last->pl_con = ACON_AND;
		else if (match(BM(TK_PIPE_PIPE)))
			last->pl_con = ACON_OR;
	} while (last->pl_con != ACON_NONE);
}

/*
 * [SYNTAX]
 * ashe_block ::= plist
 * 		| plist '&'
 * 		| plist '&' ashe_block
 *		| plist ';'
 *		| plist ';' ashe_block
 */

ASHE_PUBLIC void pblock(struct a_block *block)
{
	struct a_list list;

	a_list_init(&list);

	for (nexttok(&A_LEX); A_CTOK.type != TK_EOL; nexttok(&A_LEX)) {
		a_arr_list_push(&block->bl_lists, list);
		plist(block, a_arr_list_last(&block->bl_lists));
		ashe_assert(block->bl_subst == 0);
		if (A_CTOK.type == TK_EOL)
			break;
		ashe_assert(BM(A_CTOK.type) & BM_SEPARATOR);
	}
}

ASHE_PUBLIC a_int32 ashe_parse(const char *restrict cstr)
{
	ashe_dprint("R[E]PL");
	a_lexer_init(&ashe.sh_lexer, cstr);
	ashe.sh_buf.buf_code = 0;
	if (setjmp(ashe.sh_buf.buf_jmpbuf) == 0)
		pblock(&ashe.sh_block);
	return ashe.sh_buf.buf_code;
}
