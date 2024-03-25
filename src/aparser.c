#include "autils.h"
#include "alex.h"
#include "aparser.h"
#include "ashell.h"
#include "atoken.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ubyte is_n_redirection[] = {
	0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define isnredirect(type) is_n_redirection[type]

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
#define ERR_EXPECT 0
#define ERR_CMDSUBST 1

static const char *perrors[] = {
	"expected %s, instead got '%s'.",
	"can't have command substitution here.",
};

/* Advance to the next token. */
ASHE_PRIVATE void next_token(Lexer *lexer)
{
	lexer->prev = lexer->curr;
	lexer->curr = Lexer_next(lexer);
}

/* =========== AST =========== */

ASHE_PRIVATE inline void FileHandle_init(FileHandle *fh)
{
	memset(fh, 0, sizeof(FileHandle));
}

ASHE_PRIVATE inline void Command_init(Command *cmd)
{
	ArrayCharptr_init(&cmd->argv);
	ArrayCharptr_init(&cmd->env);
	ArrayFileHandle_init(&cmd->fhandles);
	cmd->pipand = 0;
}

ASHE_PRIVATE void Command_free(Command *cmd)
{
	// Shell global frees the 'char*'s in these arrays
	ArrayCharptr_free(&cmd->argv, NULL);
	ArrayCharptr_free(&cmd->env, NULL);
	ArrayFileHandle_free(&cmd->fhandles, NULL);
	Command_init(cmd);
}

ASHE_PRIVATE void Pipeline_init(Pipeline *pip)
{
	ArrayCommand_init(&pip->commands);
	pip->connection = CON_NONE;
}

ASHE_PRIVATE void Pipeline_free(Pipeline *pip)
{
	ArrayCommand_free(&pip->commands, (FreeFn)Command_free);
	Pipeline_init(pip);
}

ASHE_PRIVATE void Conditional_init(Conditional *cond)
{
	ArrayPipeline_init(&cond->pipelines);
	cond->is_background = 0;
}

ASHE_PUBLIC void Conditional_free(Conditional *cond)
{
	ArrayPipeline_free(&cond->pipelines, (FreeFn)Pipeline_free);
	Conditional_init(cond);
}

/* ============ PARSING ============ */

/* recursive in 'command()' */
ASHE_PRIVATE void pipeline(Lexer *lexer, ArrayPipeline *arpip, Pipeline *pip,
			   memmax depth);

ASHE_PRIVATE inline void print_input_and_invalid_token(Lexer *lexer, Token *tok)
{
	int32 startlen = tok->start - lexer->start;
	printf_error("%.*s'%s'", startlen, lexer->start, tok->start);
}

ASHE_PRIVATE inline void expect(Lexer *lexer, ubyte next, memmax bitmask,
				 const char *type)
{
	if (next)
		next_token(lexer);
	if (BM(lexer->curr.type) & bitmask)
		return;
	printf_error(perrors[ERR_EXPECT], type, Token_tostr(&lexer->curr));
	print_input_and_invalid_token(lexer, &lexer->curr);
	jump_out(-1);
}

ASHE_PRIVATE void duplicate_or_close(Lexer *lexer, FileHandle *fh)
{
	next_token(lexer);
	Tokentype type = lexer->curr.type;
	if (type != TK_MINUS) {
		if (type == TK_WORD || type == TK_KVPAIR) {
			fh->filepath = CSTR(lexer);
		} else {
			expect(lexer, 0, BM(TK_NUMBER), "file descriptor");
			fh->fddup = CNM(lexer);
		}
		return;
	}
	fh->op = OP_CLOSE;
}

ASHE_PRIVATE void redirection(Lexer *lexer, Command *cmd)
{
#define NFD_OR(have_n, fd) ((have_n) ? CNM(lexer) : (fd))

	memmax fd = 0;
	ubyte have_n = lexer->prev.type == TK_NUMBER;
	FileHandle fh;
	FileHandle_init(&fh);

	switch (lexer->curr.type) {
	case TK_AND_GREATER_GREATER:
		fh.append = 1;
		/* FALLTHRU */
	case TK_AND_GREATER:
		ashe_assert(!have_n, "parser fatal error, can't have number before '&>'");
		fh.op = OP_REDIRECT_ERROUT;
		// fd is irrelevant, won't get checked at runtime
		goto l_redirect_fin;
	case TK_LESS_GREATER:
		fh.op = OP_REDIRECT_INOUT;
		fd = STDIN_FILENO;
		goto l_redirect_fin;
	case TK_LESS:
		fh.op = OP_REDIRECT_IN;
		fd = STDIN_FILENO;
		goto l_redirect_fin;
	case TK_GREATER_AND:
		if (have_n) {
			fh.op = OP_DUP_OUT;
			fd = STDOUT_FILENO;
			goto l_dup_fin;
		}
		fh.op = OP_REDIRECT_ERROUT;
		// fd is irrelevant, won't get checked at runtime
		goto l_redirect_fin;
	case TK_GREATER_PIPE:
		cmd->cmd_settings &= ~SETTING_NOCLOBBER;
		goto l_greater;
	case TK_GREATER_GREATER:
		fh.append = 1;
		/* FALLTHRU */
	case TK_GREATER:
l_greater:
		fh.op = OP_REDIRECT_OUT;
		fd = STDOUT_FILENO;
l_redirect_fin:
		fh.fd = NFD_OR(have_n, fd);
		expect(lexer, 1, BM(TK_WORD) | BM(TK_KVPAIR), "string");
		fh.filepath = CSTR(lexer);
		break;
	case TK_LESS_AND:
		fh.op = OP_DUP_IN;
		fd = STDIN_FILENO;
l_dup_fin:
		fh.fd = NFD_OR(have_n, fd);
		duplicate_or_close(lexer, &fh);
		break;
	default:
		ashe_assert(0, "unreachable");
	}

	ArrayFileHandle_push(&cmd->fhandles, fh);

#undef NFD_OR
}

ASHE_PRIVATE int32 number(Lexer *lexer, Command *cmd)
{
	next_token(lexer);
	Tokentype type = lexer->curr.type;
	if (lexer->ws || !isnredirect(type)) {
		char *number_string = *ArrayCharptr_last(&ashe.sh_buffers);
		ArrayCharptr_push(&cmd->argv, number_string);
		return 0; // not a file descriptor (n)
	}
	redirection(lexer, cmd);
	return 1;
}

ASHE_PRIVATE ubyte get_first_command(Lexer *lexer, Command *cmd)
{
	Tokentype type;

	for (;;) {
		next_token(lexer);
		type = lexer->curr.type;
		switch (type) {
		case TK_NUMBER:;
			char *numstr = CSTR(lexer);
			next_token(lexer);
			if (lexer->ws || !isnredirect(type)) {
				ArrayCharptr_push(&cmd->argv, numstr);
				return 1;
			}
			const char *adjacent = Token_tostr(&lexer->curr);
			printf_error("expected string, instead got '%s%s'.", numstr,
				     adjacent);
			jump_out(-1);
		case TK_WORD:
			ArrayCharptr_push(&cmd->argv, CSTR(lexer));
			return 0;
		case TK_KVPAIR:
			ArrayCharptr_push(&cmd->env, CSTR(lexer));
			break;
		case TK_EOL:
			return 0;
		default:
			printf_error(perrors[ERR_EXPECT], "string",
				     Token_tostr(&lexer->curr));
			print_input_and_invalid_token(lexer, &lexer->curr);
			jump_out(-1);
		}
	}
}

ASHE_PRIVATE void command(Lexer *lexer, ArrayPipeline *arpip, Command *cmd, memmax depth)
{
	ArrayCharptr *target;

	if (get_first_command(lexer, cmd))
		goto l_switch; // we are 1 token ahead
	if (cmd->argv.len == 0)
		return; // hit TK_EOL, have only env vars

	for (;;) {
		next_token(lexer);
l_switch:
		switch (lexer->curr.type) {
		case TK_PIPE_AND:
			cmd->pipand = 1;
			return; // this is a pipeline token
		case TK_AND_GREATER:
		case TK_GREATER_PIPE:
		case TK_AND_GREATER_GREATER:
		case TK_GREATER_AND:
		case TK_GREATER_GREATER:
		case TK_GREATER:
		case TK_LESS:
		case TK_LESS_AND:
		case TK_LESS_GREATER:
			redirection(lexer, cmd);
			break;
		case TK_MINUS:
			ArrayCharptr_push(&cmd->argv, "-");
			break;
		case TK_LPAREN:;
			Tokentype prevt = lexer->prev.type;
			if (!lexer->ws && (prevt == TK_WORD || prevt == TK_NUMBER)) {
				printf_error(perrors[ERR_CMDSUBST]);
				print_input_and_invalid_token(lexer, &lexer->curr);
				jump_out(-1);
			}
			Pipeline pip;
			Pipeline_init(&pip);
			/*
                         * Have to insert immediately in order to be able to
                         * clean it up in case of errors (jump_out), this makes
                         * the logic a bit strange and clunky.
                         * We must know the recursion depth in order to correctly
                         * insert into the array, that is what the 'depth' parameter
                         * is for.
                         */
			memmax index = arpip->len - (depth + 1);
			ArrayPipeline_insert(arpip, index, pip);
			next_token(lexer);
			pipeline(lexer, arpip, &arpip->data[index], depth + 1);
			expect(lexer, 0, BM(TK_RPAREN),
			       "')' (missing end of command substitution)");
			break;
		case TK_WORD:
			target = &cmd->argv;
			goto l_push_buffer;
		case TK_KVPAIR:
			target = &cmd->env;
l_push_buffer:
			ArrayCharptr_push(target, CSTR(lexer));
			break;
		case TK_NUMBER:
			if (!number(lexer, cmd))
				goto l_switch;
			break;
		default:
			return;
		}
	}
}

ASHE_PRIVATE void pipeline(Lexer *lexer, ArrayPipeline *arpip, Pipeline *pip,
			   memmax depth)
{
	Command cmd;
	Command_init(&cmd);
	for (;;) {
		ArrayCommand_push(&pip->commands, cmd);
		Command *last = ArrayCommand_last(&pip->commands);
		command(lexer, arpip, last, depth);
		if (lexer->curr.type != TK_PIPE && lexer->curr.type != TK_PIPE_AND)
			break;
	}
}

ASHE_PRIVATE inline void conditional(Lexer *lexer, Conditional *cond)
{
	Pipeline pip;
	Pipeline_init(&pip);
	for (;;) {
		ArrayPipeline_push(&cond->pipelines, pip);
		Pipeline *last = ArrayPipeline_last(&cond->pipelines);
		pipeline(lexer, &cond->pipelines, last, 1);
		if (lexer->curr.type == TK_AND_AND)
			last->connection = CON_AND;
		else if (lexer->curr.type == TK_PIPE_PIPE)
			last->connection = CON_OR;
		if (last->connection & CON_NONE)
			break;
	}
}

ASHE_PUBLIC int32 parse(const char *str)
{
	ArrayConditional *conds = &ashe.sh_conds;
	AsheJmpBuf *jmpbuf = &ashe.sh_buf;
	Lexer *lexer = &ashe.sh_lexer;
	Lexer_init(lexer, str);
	jmpbuf->buf_code = 0;
	if (setjmp(jmpbuf->buf_jmpbuf) == 0) { // didn't jump out (setter) ?
		Tokentype type;
		Conditional cond;
		Conditional_init(&cond);
		for (;;) {
			ArrayConditional_push(conds, cond);
			Conditional *last = ArrayConditional_last(conds);
			conditional(lexer, last);
			type = lexer->curr.type;
			if (type != TK_SEMICOLON && type != TK_AND) {
				if (type != TK_EOL) {
					const char *got = Token_tostr(&lexer->curr);
					printf_error(perrors[ERR_EXPECT], "string", got);
					jmpbuf->buf_code = -1;
				}
				break;
			}
		}
	}
	return jmpbuf->buf_code;
}
