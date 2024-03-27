#include "acommon.h"
#include "autils.h"
#include "alex.h"
#include "atoken.h"
#include "ashell.h"

#include <ctype.h>
#include <stdio.h>

/*
 * TODO: Implement ****GLOB OPERATOR****, and rest of the regular expressions
 * #include <glob.h>
 */

static const char *tokenstr[] = {
	"&&",
	"||",
	"|&",
	"<&",
	">&",
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
	"",
	NULL /* WORD */,
	NULL /* KVPAIR */,
	NULL /* NUMBER */
};

/* Return 1 if character 'c' is a char token. */
ASHE_PRIVATE inline ubyte has_precedence(int32 c)
{
	switch (c) {
	case '>':
	case '<':
	case ';':
	case '(':
	case ')':
	case '|':
	case '&':
		return 1;
	default:
		return 0;
	}
}

ASHE_PRIVATE inline void Token_init(Token *token)
{
	memset(token, 0, sizeof(Token));
}

ASHE_PUBLIC void Lexer_init(Lexer *lexer, const char *start)
{
	lexer->current = lexer->start = start;
	Token_init(&lexer->curr);
	Token_init(&lexer->prev);
}

/* Peek 'amount' without advancing. */
ASHE_PRIVATE inline int32 peek(Lexer *lexer, memmax amount)
{
	return *(lexer->current + amount);
}

/* Advance buffer by a single character unless EOF is reached. */
ASHE_PRIVATE inline int32 advance(Lexer *lexer)
{
	int32 c = *lexer->current;
	if (c != '\0')
		lexer->current++;
	return c;
}

ASHE_PRIVATE int32 all_chars_are_digits(const char *str, memmax *n)
{
	int32 c;
	memmax number, prev;
	number = prev = 0;
	while (isdigit((c = *str++))) {
		number = number * 10 + (c - '0');
		if (prev >= number)
			return -2;
		prev = number;
	}
	if (c != '\0')
		return -1;
	*n = number;
	return 0;
}

/* Gets a string, expands environmental variables and unescapes it. */
ASHE_PRIVATE Token Token_string(Lexer *lexer)
{
	int32 c;
	ubyte dq, esc;
	Token token;
	Token_init(&token);
	Buffer buffer;
	Buffer_init(&buffer);

	token.type = TK_WORD;
	token.start = lexer->current;
	dq = esc = 0;

	while ((c = peek(lexer, 0))) {
		if (!dq && (isspace(c) || (!esc && has_precedence(c))))
			break;
		if (!esc && c == '"')
			dq ^= 1;
		else if (c == '\\' || esc)
			esc ^= 1;
		Buffer_push(&buffer, c);
		advance(lexer);
	}

	Buffer_push(&buffer, '\0');
	token.len = lexer->current - token.start;

	/* expand '$' variables */
	expand_vars(&buffer);

	char *ptr = buffer.data;
	if (*ptr != '=' && (ptr = strstr(ptr, "=")) != NULL) {
		*ptr = '\0';
		memmax klen = strlen(buffer.data);
		if (strspn(buffer.data, ENV_VAR_CHARS) == klen)
			token.type = TK_KVPAIR;
		*ptr = '=';
	}

	/* escapes 'escaped' characters and gets rid of '"' */
	escape(&buffer);

	memmax number = 0;
	int32 code = all_chars_are_digits(buffer.data, &number);
	if (code != -1 && code != -2) { /* No overflow and valid integer ? */
		token.u.number = number;
		token.type = TK_NUMBER;
	}

	/* Check after expanding, so that we don't compare in parser */
	if (buffer.len == 2 && *buffer.data == '-') {
		Buffer_free(&buffer, NULL);
		token.type = TK_MINUS;
	} else
		ArrayCharptr_push(&ashe.sh_buffers, buffer.data);

	return token;
}

/* Skip whitespace characters */
ASHE_PRIVATE void skipws(Lexer *lexer)
{
	lexer->ws = 0;
	int32 c = peek(lexer, 0);
	for (;;) {
		switch (c) {
		case '\n':
		case '\r':
		case ' ':
		case '\t':
		case '\v':
			lexer->ws = 1;
			advance(lexer);
			c = peek(lexer, 0);
			break;
		default:
			return;
		}
	}
}

/* Auxiliary to 'Token_tostr()' */
ASHE_PRIVATE inline const char *num2str(memmax n)
{
	static char buffer[UINT_DIGITS + 1];
	snprintf(buffer, UINT_DIGITS + 1, "%lu", n);
	return buffer;
}

/* Note: TOKEN_NUMBER stores its own string in a static
 * buffer, invoking this function second time will overwrite
 * the static buffer. */
ASHE_PUBLIC const char *Token_tostr(Token *token)
{
	switch (token->type) {
	case TK_AND_AND:
	case TK_PIPE_PIPE:
	case TK_PIPE_AND:
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
		ashe_assert(0, "unreachable");
		return NULL;
	}
}

ASHE_PRIVATE inline Token Token_new(Tokentype type)
{
	Token token;
	token.type = type;
	return token;
}

ASHE_PUBLIC Token Lexer_next(Lexer *lexer)
{
	int32 c;
	Tokentype type;

	skipws(lexer);
	if ((c = advance(lexer)) == '\0')
		return Token_new(TK_EOL);

	switch (c) {
	case '<': {
		switch (peek(lexer, 0)) {
		case '&':
			advance(lexer);
			type = TK_LESS_AND;
			break;
		case '>':
			advance(lexer);
			type = TK_LESS_GREATER;
			break;
		default:
			type = TK_LESS;
			break;
		}
		break;
	}
	case '>': {
		switch (peek(lexer, 0)) {
		case '>':
			advance(lexer);
			type = TK_GREATER_GREATER;
			break;
		case '&':
			advance(lexer);
			type = TK_GREATER_AND;
			break;
		// TODO: Implement clobber '>|'
		default:
			type = TK_GREATER;
			break;
		}
		break;
	}
	case '|': {
		switch (peek(lexer, 0)) {
		case '|':
			advance(lexer);
			type = TK_PIPE_PIPE;
			break;
		case '&':
			advance(lexer);
			type = TK_PIPE_AND;
			break;
		default:
			type = TK_PIPE;
			break;
		}
		break;
	}
	case '&': {
		switch (peek(lexer, 0)) {
		case '&':
			advance(lexer);
			type = TK_AND_AND;
			break;
		case '>':
			advance(lexer);
			if (peek(lexer, 0) == '>') {
				advance(lexer);
				type = TK_AND_GREATER_GREATER;
			} else {
				type = TK_AND_GREATER;
			}
			break;
		default:
			type = TK_AND;
			break;
		}
		break;
	}
	case '-':
		if (!isspace(peek(lexer, 0)))
			return Token_string(lexer);
		type = TK_MINUS;
		break;
	case ';':
		type = TK_SEMICOLON;
		break;
	case '(':
		type = TK_LPAREN;
		break;
	case ')':
		type = TK_RPAREN;
		break;
	default:
		return Token_string(lexer);
	}

	return Token_new(type);
}
