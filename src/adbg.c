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

#define ASHE_TAB 4

#define pushtabs(out, tabs)                           \
	for (uint32 i = 0; i < tabs; i++)             \
		for (uint32 j = 0; j < ASHE_TAB; j++) \
			a_arr_char_push(out, ' ');

#define pushsep(out) a_arr_char_push_strlit(out, ",\n")

#define a_arr_char_push_strlit(out, lit) a_arr_char_push_str(out, lit, SS(lit))

#define SS(str) (sizeof(str) - 1)

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

ASHE_PUBLIC void debug_token(struct a_token *token)
{
	ashe_dprintf("TOKEN -> '%s' [%d]", a_token_str(token), token->type);
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

ASHE_PUBLIC void debug_number(ssize n, uint32 tabs, a_arr_char *out)
{
	pushtabs(out, tabs);
	a_arr_char_push_num(out, n);
}

ASHE_PUBLIC void debug_boolean(ubyte b, uint32 tabs, a_arr_char *out)
{
	const char *boolean;

	boolean = (b ? "true" : "false");
	pushtabs(out, tabs);
	a_arr_char_push_str(out, boolean, strlen(boolean));
}

ASHE_PUBLIC void debug_cstring(const char *ptr, uint32 tabs, a_arr_char *out)
{
	const char *string;

	string = (ptr ? ptr : "(null)");
	pushtabs(out, tabs);
	a_arr_char_push(out, '"');
	a_arr_char_push_str(out, string, strlen(string));
	a_arr_char_push(out, '"');
}

/*			ENUMS				 */

ASHE_PUBLIC void debug_connect(enum a_connect con, uint32 tabs, a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
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

ASHE_PUBLIC void debug_redirect_op(enum a_redirect_op op, uint32 tabs,
				   a_arr_char *out)
{
	const char *suffix;

	pushtabs(out, tabs);
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

ASHE_PUBLIC void debug_redirect(struct a_redirect *rd, const char *name,
				uint32 tabs, a_arr_char *out)
{
	/* prefix */
	pushtabs(out, tabs);
	debug_struct_prefix("struct a_redirect", name, tabs, out);
	/* body */
	++tabs;
	debug_number(rd->rd_lhsfd, tabs, out);
	pushsep(out);
	debug_number(rd->rd_rhsfd, tabs, out);
	pushsep(out);
	debug_cstring(rd->rd_fname, tabs, out);
	pushsep(out);
	debug_redirect_op(rd->rd_op, tabs, out);
	pushsep(out);
	debug_boolean(rd->rd_append, tabs, out);
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
	debug_arr_cmd(&pipeline->pl_cmds, NULL, tabs, out);
	pushsep(out);
	debug_connect(pipeline->pl_con, tabs, out);
	pushsep(out);
	debug_boolean(pipeline->pl_bg, tabs, out);
	pushsep(out);
	debug_cstring(pipeline->pl_input, tabs, out);
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
	debug_arr_list(&block->bl_lists, NULL, tabs, out);
	pushsep(out);
	debug_number(block->bl_subst, tabs, out);
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

ASHE_PUBLIC void debug_arr_redirect(a_arr_redirect *rds, const char *name,
				    uint32 tabs, a_arr_char *out)
{
	uint32 len, i;

	len = a_arr_redirect_len(rds);
	/* prefix */
	debug_arr_prefix("a_arr_redirect", name, len, tabs, out);
	/* body */
	++tabs;
	for (i = 0; i < len; i++) {
		debug_redirect(a_arr_redirect_index(rds, i), NULL, tabs, out);
		if (i + 1 < len)
			pushsep(out);
		else
			a_arr_char_push(out, '\n');
	}
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_arr_ccharp(a_arr_ccharp *arr, const char *name,
				  uint32 tabs, a_arr_char *out)
{
	uint32 len, i;

	len = a_arr_ccharp_len(arr);
	/* prefix */
	debug_arr_prefix("a_arr_ccharp", name, len, tabs, out);
	/* body */
	++tabs;
	for (i = 0; i < len; i++) {
		debug_cstring(*a_arr_ccharp_index(arr, i), tabs, out);
		if (i + 1 < len)
			pushsep(out);
		else
			a_arr_char_push(out, '\n');
	}
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_arr_cmd(a_arr_cmd *cmds, const char *name, uint32 tabs,
			       a_arr_char *out)
{
	uint len, i;

	len = a_arr_cmd_len(cmds);
	/* prefix */
	debug_arr_prefix("a_arr_cmd", name, len, tabs, out);
	/* body */
	++tabs;
	for (i = 0; i < len; i++) {
		debug_cmd(a_arr_cmd_index(cmds, i), NULL, tabs, out);
		if (i + 1 < len)
			pushsep(out);
		else
			a_arr_char_push(out, '\n');
	}
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_arr_pipeline(a_arr_pipeline *pipes, const char *name,
				    uint32 tabs, a_arr_char *out)
{
	uint len, i;

	len = a_arr_pipeline_len(pipes);
	/* prefix */
	debug_arr_prefix("a_arr_pipeline", name, len, tabs, out);
	/* body */
	++tabs;
	for (i = 0; i < len; i++) {
		debug_pipeline(a_arr_pipeline_index(pipes, i), NULL, tabs, out);
		if (i + 1 < len)
			pushsep(out);
		else
			a_arr_char_push(out, '\n');
	}
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
}

ASHE_PUBLIC void debug_arr_list(a_arr_list *lists, const char *name,
				uint32 tabs, a_arr_char *out)
{
	uint len, i;

	len = a_arr_list_len(lists);
	/* prefix */
	debug_arr_prefix("a_arr_list", name, len, tabs, out);
	/* body */
	++tabs;
	for (i = 0; i < len; i++) {
		debug_list(a_arr_list_index(lists, i), NULL, tabs, out);
		if (i + 1 < len)
			pushsep(out);
		else
			a_arr_char_push(out, '\n');
	}
	--tabs;
	/* suffix */
	debug_suffix(tabs, out);
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

	errno = 0;
	if (unlikely((fp = fopen(logfiles[ALOG_CURSOR], "a")) == NULL))
		goto error;

	temp = snprintf(
		buffer, sizeof(buffer),
		"[TCOLMAX:%u][TCOL:%u][ROW:%u][LINE_LEN:%lu][COL:%u][IBFIDX:%lu]\r\n",
		TCOLMAX, TCOL, ROW, LINE.len, COL, IBFIDX);

	if (unlikely(temp < 0 || temp > BUFSIZ))
		goto error;

	len += temp;

	if (unlikely(fwrite(buffer, sizeof(byte), len, fp) != len))
		goto error;
	if (unlikely(fclose(fp) == EOF))
		goto error;

	return;
error:
	fclose(fp);
	ashe_perrno();
	panic("panic in debug_cursor");
}

/* Debug each row and its line (contents) */
ASHE_PUBLIC void debug_lines(void)
{
	static char temp[128];
	a_arr_char buffer;
	ssize len = 0;
	uint32 i = 0;
	uint32 idx;
	FILE *fp;

	errno = 0;
	a_arr_char_init(&buffer);

	if (unlikely((fp = fopen(logfiles[ALOG_LINES], "w")) == NULL))
		goto error;

	/* Log prompt */
	if (unlikely((len = snprintf(temp, sizeof(temp), "[PLEN:%lu] -> [",
				     PLEN)) < 0 ||
		     (memmax)len > sizeof(temp)))
		goto error;
	a_arr_char_push_str(&buffer, temp, len);
	a_arr_char_push_str(&buffer, ashe.sh_prompt.data,
			    ashe.sh_prompt.len - 1);
	a_arr_char_push_strlit(&buffer, "]\r\n");

	/* Log buffer */
	if (unlikely((len = snprintf(temp, sizeof(temp), "[IBFLEN:%u] -> [",
				     IBF.len)) < 0 ||
		     (memmax)len > sizeof(temp)))
		goto error;
	a_arr_char_push_str(&buffer, temp, len);
	idx = buffer.len;
	a_arr_char_push_str(&buffer, IBF.data, IBF.len);
	unescape(&buffer, idx, buffer.len);
	a_arr_char_push_strlit(&buffer, "]\r\n");

	/* Log lines */
	for (i = 0; i < LINES.len; i++) {
		if (unlikely((len = snprintf(temp, sizeof(temp),
					     "[LINE:%4u][LEN:%4lu] -> [", i,
					     LINES.data[i].len)) < 0 ||
			     (memmax)len > sizeof(temp)))
			goto error;
		a_arr_char_push_str(&buffer, temp, len);
		idx = buffer.len;
		a_arr_char_push_str(&buffer, LINES.data[i].start,
				    LINES.data[i].len);
		unescape(&buffer, idx, buffer.len);
		a_arr_char_push_strlit(&buffer, "]\r\n");
	}

	/* Write log file */
	if (unlikely(fwrite(buffer.data, sizeof(byte), buffer.len, fp) <
		     buffer.len))
		goto error;
	if (unlikely(fclose(fp) == EOF))
		goto fclose_error;

	a_arr_char_free(&buffer, NULL);
	return;
error:
	fclose(fp);
fclose_error:
	a_arr_char_free(&buffer, NULL);
	ashe_perrno();
	panic("panic in debug_lines()");
}

ASHE_PUBLIC void remove_logfiles(void)
{
	DIR *root;
	struct dirent *entry;

	errno = 0;
	if ((root = opendir(".")) == NULL)
		goto error;
	for (errno = 0; (entry = readdir(root)) != NULL; errno = 0) {
		if ((strcmp(entry->d_name, logfiles[ALOG_CURSOR]) == 0 ||
		     strcmp(entry->d_name, logfiles[ALOG_LINES]) == 0) &&
		    entry->d_type == DT_REG) {
			if (unlink(entry->d_name) < 0)
				ashe_perrno(); /* still try unlink other logfiles */
		}
	}
	if (errno != 0)
		goto error;

	closedir(root);
	return;
error:
	closedir(root);
	ashe_perrno();
	panic("panic in remove_logfiles()");
}

ASHE_PUBLIC void logfile_create(const char *logfile, int32 which)
{
	logfiles[which] = logfile;
	if (unlikely(fopen(logfile, "w") == NULL)) {
		ashe_perrno();
		panic("panic in logfile_create(\"%s\", %d)", logfile, which);
	}
}
