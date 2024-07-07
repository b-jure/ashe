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

#include "acommon.h"
#include "ainput.h"
#include "autils.h"
#include "aparser.h"
#include "autils.h"
#include "adbg.h"
#include "ashell.h"

#include <stdio.h>
#include <errno.h>
#include <dirent.h>

/* push tabs for proper indentation */
#define pushtabs(out, tabs)                            \
	for (a_uint32 i = 0; i < tabs * ASHE_TAB; i++) \
		a_arr_char_push(out, ' ');

/* push struct/array value separator */
#define pushsep(out) a_arr_char_push_strlit(out, ",\n")

/* push struct field name */
#define pushfieldname(out, name) a_arr_char_push_strf(out, "%s: ", name)

/* generic array index function */
#define refidxfn(type)	 a_arr_##type##_index
#define derefidxfn(type) *a_arr_##type##_index

/* debug generic array */
#define debug_arr(type, ptr, name, tabs, out, idxfn) \
	debug_arr_internal(type, ptr, name, tabs, out, idxfn)

#define debug_arr_internal(type, ptr, name, tabs, out, idxfn)           \
	do {                                                            \
		a_uint32 len, i;                                        \
                                                                        \
		len = a_arrp_len(ptr);                                  \
		debug_arr_prefix("a_arr_" #type, name, len, tabs, out); \
		++tabs;                                                 \
		for (i = 0; i < len; i++) {                             \
			debug_##type(idxfn(ptr, i), NULL, tabs, out);   \
			if (i + 1 < len)                                \
				pushsep(out);                           \
			else                                            \
				a_arr_char_push(out, '\n');             \
		}                                                       \
		--tabs;                                                 \
		debug_suffix(tabs, out);                                \
	} while (0)

const char *logfiles[] = {
	NULL,
	NULL,
};

static const char *tokenstr[] = {
	"&&",
	"||",
	"<&",
	">&",
	">|",
	">>",
	"&>",
	"&>>",
	"<>",
	"<",
	">",
	"-",
	";",
	"(",
	")",
	"|",
	"&",
	"EOL",
	NULL /* WORD */,
	NULL /* KVPAIR */,
	NULL /* NUMBER */
};

/*
 *
 *			DEBUG TOKENS
 *
 */

/* Auxiliary to 'a_token_debug()' */
ASHE_PRIVATE inline const char *num2str(a_memmax n)
{
	static char buffer[ASHE_MAXNUMSTR + 1];
	ashe_snprintf(buffer, sizeof(buffer) - 1, "%zu", n);
	return buffer;
}

ASHE_PUBLIC const char *a_token_str(struct a_token *token)
{
	switch (token->type) {
	case TK_AND_AND:
	case TK_PIPE_PIPE:
	case TK_LESS_AND:
	case TK_GREATER_AND:
	case TK_GREATER_GREATER:
	case TK_AND_GREATER:
	case TK_AND_GREATER_GREATER:
	case TK_LESS_GREATER:
	case TK_LESS:
	case TK_GREATER:
	case TK_MINUS:
	case TK_SEMICOLON:
	case TK_LPAREN:
	case TK_RPAREN:
	case TK_PIPE:
	case TK_AND:
	case TK_EOL:
		return tokenstr[token->type];
	case TK_WORD:
	case TK_KVPAIR:
		return token->u.string.data;
	case TK_NUMBER:
		return num2str(token->u.number);
	default:
		/* UNREACHED */
		ashe_panic("unreachable");
		return NULL;
	}
}

/*
 *
 *			DEBUG AST
 *
 */

ASHE_PRIVATE void debug_prefix(const char *type, const char *name, a_uint32 tabs, a_arr_char *out)
{
	pushtabs(out, tabs);
	a_arr_char_push_strf(out, "%s ", type);
	if (name != NULL)
		a_arr_char_push_strf(out, "%s ", name);
}

ASHE_PRIVATE void debug_suffix(a_uint32 tabs, a_arr_char *out)
{
	pushtabs(out, tabs);
	a_arr_char_push(out, '}');
}

/*			TERMS				*/

ASHE_PUBLIC void debug_number(a_ssize n, const char *name, a_uint32 tabs, a_arr_char *out)
{
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_number(out, n);
}

ASHE_PUBLIC void debug_boolean(a_ubyte b, const char *name, a_uint32 tabs, a_arr_char *out)
{
	const char *boolean;

	boolean = (b ? "true" : "false");
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_str(out, boolean, strlen(boolean));
}

ASHE_PUBLIC void debug_ptr(const void *ptr, const char *name, a_uint32 tabs, a_arr_char *out)
{
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_ptr(out, ptr);
}

ASHE_PUBLIC void debug_string(const char *str, a_memmax len, const char *name, a_uint32 tabs,
			      a_arr_char *out)
{
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_str(out, str, len);
}

ASHE_PUBLIC void debug_ccharp(const char *ptr, const char *name, a_uint32 tabs, a_arr_char *out)
{
	const char *string;

	string = (ptr ? ptr : "(null)");
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_strf(out, "\"%s\"", string);
}

/*			ENUMS				*/

ASHE_PUBLIC void debug_toktype(enum a_toktype type, const char *name, a_uint32 tabs,
			       a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	switch (type) {
	case TK_AND_AND:
		suffix = "AND_AND";
		break;
	case TK_PIPE_PIPE:
		suffix = "PIPE_PIPE";
		break;
	case TK_LESS_AND:
		suffix = "LESS_AND";
		break;
	case TK_GREATER_AND:
		suffix = "GREATER_AND";
		break;
	case TK_GREATER_PIPE:
		suffix = "GREATER_PIPE";
		break;
	case TK_GREATER_GREATER:
		suffix = "GREATER_GREATER";
		break;
	case TK_AND_GREATER:
		suffix = "AND_GREATER";
		break;
	case TK_AND_GREATER_GREATER:
		suffix = "AND_GREATER_GREATER";
		break;
	case TK_LESS_GREATER:
		suffix = "LESS_GREATER";
		break;
	case TK_LESS:
		suffix = "LESS";
		break;
	case TK_GREATER:
		suffix = "GREATER";
		break;
	case TK_MINUS:
		suffix = "MINUS";
		break;
	case TK_SEMICOLON:
		suffix = "SEMICOLON";
		break;
	case TK_LPAREN:
		suffix = "LPAREN";
		break;
	case TK_RPAREN:
		suffix = "RPAREN";
		break;
	case TK_PIPE:
		suffix = "PIPE";
		break;
	case TK_AND:
		suffix = "AND";
		break;
	case TK_EOL:
		suffix = "EOL";
		break;
	case TK_WORD:
		suffix = "WORD";
		break;
	case TK_KVPAIR:
		suffix = "KVPAIR";
		break;
	case TK_NUMBER:
		suffix = "NUMBER";
		break;
	default:
		/* UNREACHED */
		ashe_panic("unreachable");
		break;
	}
	a_arr_char_push_strf(out, "TK_%s", suffix);
}

ASHE_PUBLIC void debug_connect(enum a_connect con, const char *name, a_uint32 tabs, a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	switch (con) {
	case ACON_NONE:
		suffix = "NONE";
		break;
	case ACON_AND:
		suffix = "AND";
		break;
	case ACON_OR:
		suffix = "OR";
		break;
	default: /* UNREACHED */
		ashe_panic("unreachable");
		break;
	}
	a_arr_char_push_strf(out, "ACON_%s", suffix);
}

ASHE_PUBLIC void debug_redirect_op(enum a_redirect_op op, const char *name, a_uint32 tabs,
				   a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	switch (op) {
	case ARDOP_REDIRECT_ERROUT:
		suffix = "REDIRECT_ERROUT";
		break;
	case ARDOP_REDIRECT_INOUT:
		suffix = "REDIRECT_INOUT";
		break;
	case ARDOP_REDIRECT_IN:
		suffix = "REDIRECT_IN";
		break;
	case ARDOP_REDIRECT_OUT:
		suffix = "REDIRECT_OUT";
		break;
	case ARDOP_REDIRECT_CLOB:
		suffix = "REDIRECT_CLOB";
		break;
	case ARDOP_DUP_IN:
		suffix = "DUP_IN";
		break;
	case ARDOP_DUP_OUT:
		suffix = "DUP_OUT";
		break;
	case ARDOP_CLOSE:
		a_arr_char_push_strlit(out, "CLOSE");
		return;
	default: /* UNREACHED */
		ashe_panic("unreachable");
		return;
	}
	a_arr_char_push_strf(out, "ARDOP_%s", suffix);
}

/*			STRUCTS				 */

ASHE_PRIVATE void debug_struct_prefix(const char *type, const char *name, a_uint32 tabs,
				      a_arr_char *out)
{
	debug_prefix(type, name, tabs, out);
	a_arr_char_push_strlit(out, "{\n");
}

ASHE_PUBLIC void debug_token(struct a_token *tok, const char *name, a_uint32 tabs, a_arr_char *out)
{
	/* prefix */
	debug_struct_prefix("struct a_token", name, tabs, out);
	/* body */
	++tabs;
	debug_toktype(tok->type, "type", tabs, out);
	pushsep(out);
	if (tok->type == TK_NUMBER) {
		debug_number(tok->u.number, "u.number", tabs, out);
		pushsep(out);
	} else if (tok->type == TK_WORD || tok->type == TK_KVPAIR) {
		debug_ccharp(tok->u.string.data, "u.string", tabs, out);
		pushsep(out);
	}
	debug_ptr(tok->start, "start", tabs, out);
	pushsep(out);
	debug_ptr(tok->end, "end", tabs, out);
	pushsep(out);
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_redirect(struct a_redirect *rd, const char *name, a_uint32 tabs,
				a_arr_char *out)
{
	/* prefix */
	debug_struct_prefix("struct a_redirect", name, tabs, out);
	/* body */
	++tabs;
	debug_number(rd->rd_lhsfd, "rd_lhsfd", tabs, out);
	pushsep(out);
	debug_number(rd->rd_rhsfd, "rd_rhsfd", tabs, out);
	pushsep(out);
	debug_ccharp(rd->rd_fname, "rd_fname", tabs, out);
	pushsep(out);
	debug_redirect_op(rd->rd_op, "rd_op", tabs, out);
	pushsep(out);
	debug_boolean(rd->rd_append, "rd_append", tabs, out);
	a_arr_char_push(out, '\n');
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_simple_cmd(struct a_simple_cmd *scmd, const char *name, a_uint32 tabs,
				  a_arr_char *out)
{
	/* prefix */
	debug_struct_prefix("struct a_simple_cmd", name, tabs, out);
	/* body */
	++tabs;
	debug_arr_ccharp(&scmd->sc_argv, "argv", tabs, out);
	pushsep(out);
	debug_arr_ccharp(&scmd->sc_env, "env", tabs, out);
	pushsep(out);
	debug_arr_redirect(&scmd->sc_rds, "rds", tabs, out);
	a_arr_char_push(out, '\n');
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_cmd(struct a_cmd *cmd, const char *name, a_uint32 tabs, a_arr_char *out)
{
	switch (cmd->c_type) {
	case ACMD_SIMPLE:
		debug_simple_cmd(&cmd->c_u.scmd, name, tabs, out);
		break;
	default:
		/* UNREACHED */
		ashe_panic("unreachable");
		break;
	}
}

ASHE_PUBLIC void debug_pipeline(struct a_pipeline *pipeline, const char *name, a_uint32 tabs,
				a_arr_char *out)
{
	/* prefix */
	debug_struct_prefix("struct a_pipeline", name, tabs, out);
	/* body */
	++tabs;
	debug_arr_cmd(&pipeline->pl_cmds, "pl_cmds", tabs, out);
	pushsep(out);
	debug_connect(pipeline->pl_con, "pl_con", tabs, out);
	pushsep(out);
	debug_boolean(pipeline->pl_bg, "pl_bg", tabs, out);
	pushsep(out);
	debug_ccharp(pipeline->pl_input, "pl_input", tabs, out);
	a_arr_char_push(out, '\n');
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_list(struct a_list *list, const char *name, a_uint32 tabs, a_arr_char *out)
{
	/* prefix */
	debug_struct_prefix("struct a_list", name, tabs, out);
	/* body */
	++tabs;
	debug_arr_pipeline(&list->ls_pipes, NULL, tabs, out);
	a_arr_char_push(out, '\n');
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_block(struct a_block *block, const char *name, a_uint32 tabs,
			     a_arr_char *out)
{
	/* prefix */
	debug_struct_prefix("struct a_block", name, tabs, out);
	/* body */
	++tabs;
	debug_arr_list(&block->bl_lists, "bl_lists", tabs, out);
	pushsep(out);
	debug_number(block->bl_subst, "bl_subst", tabs, out);
	a_arr_char_push(out, '\n');
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

/*
 *			ARRAYS
 */

ASHE_PRIVATE void debug_arr_prefix(const char *type, const char *name, a_uint32 len, a_uint32 tabs,
				   a_arr_char *out)
{
	debug_prefix(type, name, tabs, out);
	a_arr_char_push_strf(out, "[%n] = {\n", len);
}

ASHE_PUBLIC void debug_arr_ccharp(a_arr_ccharp *ccharps, const char *name, a_uint32 tabs,
				  a_arr_char *out)
{
	debug_arr(ccharp, ccharps, name, tabs, out, derefidxfn(ccharp));
}

ASHE_PUBLIC void debug_arr_redirect(a_arr_redirect *rds, const char *name, a_uint32 tabs,
				    a_arr_char *out)
{
	debug_arr(redirect, rds, name, tabs, out, refidxfn(redirect));
}

ASHE_PUBLIC void debug_arr_cmd(a_arr_cmd *cmds, const char *name, a_uint32 tabs, a_arr_char *out)
{
	debug_arr(cmd, cmds, name, tabs, out, refidxfn(cmd));
}

ASHE_PUBLIC void debug_arr_pipeline(a_arr_pipeline *pipes, const char *name, a_uint32 tabs,
				    a_arr_char *out)
{
	debug_arr(pipeline, pipes, name, tabs, out, refidxfn(pipeline));
}

ASHE_PUBLIC void debug_arr_list(a_arr_list *lists, const char *name, a_uint32 tabs, a_arr_char *out)
{
	debug_arr(list, lists, name, tabs, out, refidxfn(list));
}

/*			ASHE DEBUG			*/

ASHE_PUBLIC void debug_current_token(struct a_token *token)
{
	a_arr_char out;

	a_arr_char_init(&out);
	debug_token(token, NULL, 0, &out);
	a_arr_char_push(&out, '\0');
	ashe_dprintf("got token...\n```\n%s\n```", out.data);
	a_arr_char_free(&out, NULL);
}

ASHE_PUBLIC void debug_ast(struct a_block *block)
{
	a_arr_char out;

	a_arr_char_init(&out);
	debug_block(block, NULL, 0, &out);
	a_arr_char_push(&out, '\0');
	ashe_dprintf("dumping AST...\n'''\n%s\n'''", out.data);
	a_arr_char_free(&out, NULL);
}

/*
 *
 *			TERMINAL INPUT
 *
 */

/* Debug cursor position */
ASHE_PUBLIC void debug_cursor(void)
{
	a_arr_char buffer;
	a_int32 fd;

	a_arr_char_init(&buffer);

	if (a_unlikely((fd = ashe_open(logfiles[ALOG_CURSOR], AHOW_RW, 1)) < 0))
		ashe_panic_libwcall(ashe_open, "can't open logfile for cursor logging");
	a_arr_char_push_strf(
		&buffer,
		"[TCOLMAX:%n][TROWMAX:%n][TCOL:%n][TROW:%n][ROW:%n][LINE_LEN:%n][COL:%n][IBFIDX:%n]\n",
		A_TCOLMAX, A_TROWMAX, A_TCOL, A_TROW, A_IROW, A_ILINE.len, A_ICOL, A_IBFIDX);
	ashe_write(fd, a_arr_ptr(buffer), a_arr_len(buffer));

	ashe_close(fd);
	a_arr_char_free(&buffer, NULL);
}

/* Debug each row and its line (contents) */
ASHE_PUBLIC void debug_lines(void)
{
	a_arr_char buffer;
	struct a_line *line;
	a_uint32 i;
	a_int32 fd;

	a_arr_char_init(&buffer);

	if (a_unlikely((fd = ashe_open(logfiles[ALOG_LINES], AHOW_W, 0)) < 0))
		ashe_panic_libwcall(ashe_open, "can't open logfile for logging lines");

	a_arr_char_push_strf(&buffer, "[A_TPLEN:%n] -> [", A_TPLEN);
	a_arr_char_push_str(&buffer, a_arr_ptr(A_TP), A_TPLEN);
	a_arr_char_push_strf(&buffer, "]\n[IBFLEN:%n] -> [", a_arr_len(A_IBF));
	a_arr_char_push_str(&buffer, a_arr_ptr(A_IBF), a_arr_len(A_IBF));
	a_arr_char_push_strlit(&buffer, "]\n");
	for (i = 0; i < A_ILINES.len; i++) {
		line = a_arr_line_index(&A_ILINES, i);
		a_arr_char_push_strf(&buffer, "[A_ILINE:%n][LEN:%n] -> [", i, line->len);
		a_arr_char_push_str(&buffer, line->start, line->len);
		a_arr_char_push_strlit(&buffer, "]\n");
	}

	ashe_write(fd, a_arr_ptr(buffer), a_arr_len(buffer));
	ashe_close(fd);
	a_arr_char_free(&buffer, NULL);
}

ASHE_PUBLIC void remove_logfiles(void)
{
	DIR *root;
	struct dirent *entry;
	a_int32 status;

	errno = 0;
	status = 0;
	root = NULL;

	if ((root = opendir(".")) == NULL)
		a_defer(-1);

	for (errno = 0; (entry = readdir(root)) != NULL;) {
		if ((strcmp(entry->d_name, logfiles[ALOG_CURSOR]) == 0 ||
		     strcmp(entry->d_name, logfiles[ALOG_LINES]) == 0) &&
		    entry->d_type == DT_REG) {
			if (unlink(entry->d_name) < 0)
				ashe_perrno("couldn't unlink %n", entry->d_name);
		}
	}

	if (a_unlikely(errno != 0 || closedir(root) < 0))
		a_defer(-1);
defer:
	if (root)
		closedir(root);
	if (status == -1) {
		closedir(root);
		ashe_perrno(NULL);
		ashe_panic(NULL);
	}
}

ASHE_PUBLIC void logfile_create(const char *logfile, a_int32 which)
{
	a_int32 fd;

	logfiles[which] = logfile;
	if (a_unlikely((fd = ashe_open(logfile, AHOW_W, 0)) < 0))
		ashe_panic_libwcall(ashe_open, "can't create logfile");
	ashe_close(fd);
}
