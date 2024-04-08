#include "acommon.h"
#include "ainput.h"
#include "aparser.h"
#include "autils.h"
#include "adbg.h"
#include "aprompt.h"
#include "ashell.h" /* avoid recursive include in 'ainput.h' */

#include <stdio.h>
#include <errno.h>
#include <dirent.h>

/* push string literal */
#define SS(str)				 (sizeof(str) - 1)
#define a_arr_char_push_strlit(out, lit) a_arr_char_push_str(out, lit, SS(lit))

/* push tabs for proper indentation */
#define ASHE_TAB 4
#define pushtabs(out, tabs)                           \
	for (uint32 i = 0; i < tabs; i++)             \
		for (uint32 j = 0; j < ASHE_TAB; j++) \
			a_arr_char_push(out, ' ');

/* push struct/array value separator */
#define pushsep(out) a_arr_char_push_strlit(out, ",\n")
/* push struct field name */
#define pushfieldname(out, name)                              \
	do {                                                  \
		a_arr_char_push_str(out, name, strlen(name)); \
		a_arr_char_push_strlit(out, ": ");            \
	} while (0)

/* generic array index function */
#define refidxfn(type)	 a_arr_##type##_index
#define derefidxfn(type) *a_arr_##type##_index
/* debug generic array */
#define debug_arr(type, ptr, name, tabs, out, idxfn) \
	debug_arr_internal(type, ptr, name, tabs, out, idxfn)
#define debug_arr_internal(type, ptr, name, tabs, out, idxfn)           \
	do {                                                            \
		uint32 len, i;                                          \
                                                                        \
		len = a_arr_##type##_len(ptr);                          \
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
ASHE_PRIVATE inline const char *num2str(memmax n)
{
	static char buffer[UINT_DIGITS + 1];

	snprintf(buffer, UINT_DIGITS + 1, "%lu", n);
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
		ashe_assert(0);
		return NULL;
	}
}

/*
 *
 *			DEBUG AST
 *
 */

ASHE_PRIVATE void debug_prefix(const char *type, const char *name, uint32 tabs,
			       a_arr_char *out)
{
	pushtabs(out, tabs);
	a_arr_char_push_str(out, type, strlen(type));
	a_arr_char_push(out, ' ');
	if (name != NULL) {
		a_arr_char_push_str(out, name, strlen(name));
		a_arr_char_push(out, ' ');
	}
}

ASHE_PRIVATE void debug_suffix(uint32 tabs, a_arr_char *out)
{
	pushtabs(out, tabs);
	a_arr_char_push(out, '}');
}

/*			TERMS				*/

ASHE_PUBLIC void debug_number(ssize n, const char *name, uint32 tabs,
			      a_arr_char *out)
{
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_num(out, n);
}

ASHE_PUBLIC void debug_boolean(ubyte b, const char *name, uint32 tabs,
			       a_arr_char *out)
{
	const char *boolean;

	boolean = (b ? "true" : "false");
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_str(out, boolean, strlen(boolean));
}

ASHE_PUBLIC void debug_ptr(const void *ptr, const char *name, uint32 tabs,
			   a_arr_char *out)
{
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_ptr(out, ptr);
}

ASHE_PUBLIC void debug_string(const char *str, memmax len, const char *name,
			      uint32 tabs, a_arr_char *out)
{
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_str(out, str, len);
}

ASHE_PUBLIC void debug_ccharp(const char *ptr, const char *name, uint32 tabs,
			      a_arr_char *out)
{
	const char *string;

	string = (ptr ? ptr : "(null)");
	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push(out, '"');
	a_arr_char_push_str(out, string, strlen(string));
	a_arr_char_push(out, '"');
}

/*			ENUMS				*/

ASHE_PUBLIC void debug_toktype(enum a_toktype type, const char *name,
			       uint32 tabs, a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_strlit(out, "TK_");
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
		ashe_assert(0);
		break;
	}
	a_arr_char_push_str(out, suffix, strlen(suffix));
}

ASHE_PUBLIC void debug_connect(enum a_connect con, const char *name,
			       uint32 tabs, a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_strlit(out, "ACON_");
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
		ashe_assert(0);
		break;
	}
	a_arr_char_push_str(out, suffix, strlen(suffix));
}

ASHE_PUBLIC void debug_redirect_op(enum a_redirect_op op, const char *name,
				   uint32 tabs, a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
	if (name != NULL)
		pushfieldname(out, name);
	a_arr_char_push_strlit(out, "ARDOP_");
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
		ashe_assert(0);
		return;
	}
	a_arr_char_push_str(out, suffix, strlen(suffix));
}

/*			STRUCTS				 */

ASHE_PRIVATE void debug_struct_prefix(const char *type, const char *name,
				      uint32 tabs, a_arr_char *out)
{
	debug_prefix(type, name, tabs, out);
	a_arr_char_push_strlit(out, "{\n");
}

ASHE_PUBLIC void debug_token(struct a_token *tok, const char *name, uint32 tabs,
			     a_arr_char *out)
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

ASHE_PUBLIC void debug_redirect(struct a_redirect *rd, const char *name,
				uint32 tabs, a_arr_char *out)
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

ASHE_PUBLIC void debug_simple_cmd(struct a_simple_cmd *scmd, const char *name,
				  uint32 tabs, a_arr_char *out)
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

ASHE_PUBLIC void debug_cmd(struct a_cmd *cmd, const char *name, uint32 tabs,
			   a_arr_char *out)
{
	switch (cmd->c_type) {
	case ACMD_SIMPLE:
		debug_simple_cmd(&cmd->c_u.scmd, name, tabs, out);
		break;
	default:
		/* UNREACHED */
		ashe_assert(0);
		break;
	}
}

ASHE_PUBLIC void debug_pipeline(struct a_pipeline *pipeline, const char *name,
				uint32 tabs, a_arr_char *out)
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

ASHE_PUBLIC void debug_list(struct a_list *list, const char *name, uint32 tabs,
			    a_arr_char *out)
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

ASHE_PUBLIC void debug_block(struct a_block *block, const char *name,
			     uint32 tabs, a_arr_char *out)
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

ASHE_PRIVATE void debug_arr_prefix(const char *type, const char *name,
				   uint32 len, uint32 tabs, a_arr_char *out)
{
	debug_prefix(type, name, tabs, out);
	a_arr_char_push(out, '[');
	a_arr_char_push_num(out, len);
	a_arr_char_push_strlit(out, "] = {\n");
}

ASHE_PUBLIC void debug_arr_ccharp(a_arr_ccharp *ccharps, const char *name,
				  uint32 tabs, a_arr_char *out)
{
	debug_arr(ccharp, ccharps, name, tabs, out, derefidxfn(ccharp));
}

ASHE_PUBLIC void debug_arr_redirect(a_arr_redirect *rds, const char *name,
				    uint32 tabs, a_arr_char *out)
{
	debug_arr(redirect, rds, name, tabs, out, refidxfn(redirect));
}

ASHE_PUBLIC void debug_arr_cmd(a_arr_cmd *cmds, const char *name, uint32 tabs,
			       a_arr_char *out)
{
	debug_arr(cmd, cmds, name, tabs, out, refidxfn(cmd));
}

ASHE_PUBLIC void debug_arr_pipeline(a_arr_pipeline *pipes, const char *name,
				    uint32 tabs, a_arr_char *out)
{
	debug_arr(pipeline, pipes, name, tabs, out, refidxfn(pipeline));
}

ASHE_PUBLIC void debug_arr_list(a_arr_list *lists, const char *name,
				uint32 tabs, a_arr_char *out)
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
	static char buffer[1028];
	FILE *fp = NULL;
	memmax len = 0;
	ssize temp = 0;
	int32 status;

	status = 0;
	errno = 0;
	if (unlikely((fp = fopen(logfiles[ALOG_CURSOR], "a")) == NULL))
		defer(-1);

	temp = snprintf(
		buffer, sizeof(buffer),
		"[A_TCOLMAX:%u][A_TCOL:%u][A_ROW:%u][LINE_LEN:%lu][A_COL:%u][A_IBFIDX:%lu]\r\n",
		A_TCOLMAX, A_TCOL, A_ROW, A_LINE.len, A_COL, A_IBFIDX);

	if (unlikely(temp < 0 || temp > BUFSIZ))
		defer(-1);

	len += temp;

	if (unlikely(fwrite(buffer, sizeof(byte), len, fp) != len))
		defer(-1);
	if (unlikely(fclose(fp) == EOF))
		defer(-1);
defer:
	if (status == -1) {
		if (fp)
			fclose(fp);
		ashe_perrno(NULL);
		ashe_panic(NULL);
	}
}

/* Debug each row and its line (contents) */
ASHE_PUBLIC void debug_lines(void)
{
	static char temp[128];
	a_arr_char buffer;
	FILE *fp = NULL;
	uint32 i, idx;
	int32 status;
	ssize len;

	status = 0;
	errno = 0;
	a_arr_char_init(&buffer);

	if (unlikely((fp = fopen(logfiles[ALOG_LINES], "w")) == NULL))
		defer(-1);

	/* Log prompt */
	if (unlikely((len = snprintf(temp, sizeof(temp), "[A_PLEN:%lu] -> [",
				     A_PLEN)) < 0 ||
		     (memmax)len > sizeof(temp)))
		defer(-1);
	a_arr_char_push_str(&buffer, temp, len);
	a_arr_char_push_str(&buffer, ashe.sh_prompt.data,
			    ashe.sh_prompt.len - 1);
	a_arr_char_push_strlit(&buffer, "]\r\n");

	/* Log buffer */
	if (unlikely((len = snprintf(temp, sizeof(temp), "[IBFLEN:%u] -> [",
				     A_IBF.len)) < 0 ||
		     (memmax)len > sizeof(temp)))
		defer(-1);
	a_arr_char_push_str(&buffer, temp, len);
	idx = buffer.len;
	a_arr_char_push_str(&buffer, A_IBF.data, A_IBF.len);
	ashe_unescape(&buffer, idx, buffer.len);
	a_arr_char_push_strlit(&buffer, "]\r\n");

	/* Log lines */
	for (i = 0; i < A_LINES.len; i++) {
		if (unlikely((len = snprintf(temp, sizeof(temp),
					     "[A_LINE:%4u][LEN:%4lu] -> [", i,
					     A_LINES.data[i].len)) < 0 ||
			     (memmax)len > sizeof(temp)))
			defer(-1);
		a_arr_char_push_str(&buffer, temp, len);
		idx = buffer.len;
		a_arr_char_push_str(&buffer, A_LINES.data[i].start,
				    A_LINES.data[i].len);
		ashe_unescape(&buffer, idx, buffer.len);
		a_arr_char_push_strlit(&buffer, "]\r\n");
	}

	/* Write log file */
	if (unlikely(fwrite(buffer.data, sizeof(byte), buffer.len, fp) <
		     buffer.len))
		defer(-1);
	if (unlikely(fclose(fp) == EOF)) {
		fp = NULL;
		defer(-1);
	}

	a_arr_char_free(&buffer, NULL);
defer:
	if (status == -1) {
		if (fp)
			fclose(fp);
		a_arr_char_free(&buffer, NULL);
		ashe_perrno(NULL);
		ashe_panic(NULL);
	}
}

// clang-format off
ASHE_PUBLIC void remove_logfiles(void)
{
	DIR *root;
	struct dirent *entry;
	int32 status;

	errno = 0;
	status = 0;
	root = NULL;
	if ((root = opendir(".")) == NULL)
		defer(-1);
	for (errno = 0; (entry = readdir(root)) != NULL;) {
		if ((strcmp(entry->d_name, logfiles[ALOG_CURSOR]) == 0 ||
		     strcmp(entry->d_name, logfiles[ALOG_LINES]) == 0) &&
		    entry->d_type == DT_REG) {
			if (unlink(entry->d_name) < 0)
				ashe_perrno("couldn't unlink %d", entry->d_name);
		}
	}
	if (unlikely(errno != 0 || closedir(root) < 0))
		defer(-1);
defer:
	if (root)
		closedir(root);
	if (status == -1) {
		closedir(root);
		ashe_perrno(NULL);
		ashe_panic(NULL);
	}
}
// clang-format on

ASHE_PUBLIC void logfile_create(const char *logfile, int32 which)
{
	logfiles[which] = logfile;
	if (unlikely(fopen(logfile, "w") == NULL)) {
		ashe_perrno("failed opening %s file", logfile);
		ashe_panic(NULL);
	}
}
