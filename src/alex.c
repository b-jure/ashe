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

/* Return 1 if character 'c' is a char token. */
ASHE_PRIVATE inline a_ubyte has_precedence(a_int32 c)
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

ASHE_PRIVATE inline void a_token_init(struct a_token *token)
{
	memset(token, 0, sizeof(struct a_token));
	token->type = TK_EOL;
}

ASHE_PUBLIC void a_lexer_init(struct a_lexer *lexer, const char *start)
{
	lexer->current = lexer->start = start;
	a_token_init(&A_CTOK);
	a_token_init(&A_PTOK);
}

/* Peek 'amount' without advancing. */
ASHE_PRIVATE inline a_int32 peek(struct a_lexer *lexer, a_memmax amount)
{
	return lexer->current[amount];
}

/* Advance buffer by a single character unless EOF is reached. */
ASHE_PRIVATE inline a_int32 advance(struct a_lexer *lexer)
{
	a_int32 c = *lexer->current;

	if (ASHE_LIKELY(c != '\0'))
		lexer->current++;
	return c;
}

ASHE_PRIVATE a_int32 all_chars_are_digits(const char *str, a_memmax *n)
{
	a_int32 c;
	a_memmax number, prev;

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
ASHE_PRIVATE struct a_token a_token_string(struct a_lexer *lexer)
{
	struct a_token token = { 0 };
	a_arr_char buffer;
	a_memmax n, klen;
	char *ptr;
	a_int32 c, code;
	a_ubyte dq, esc;

	a_arr_char_init(&buffer);
	token.type = TK_WORD;
	token.start = lexer->current;
	dq = esc = 0;

	while ((c = peek(lexer, 0))) {
		if (!dq && (isspace(c) || (!esc && has_precedence(c))))
			break;
		dq ^= (!esc && c == '"');
		esc ^= (c == '\\' || esc);
		a_arr_char_push(&buffer, c);
		advance(lexer);
	}
	token.end = lexer->current;
	a_arr_char_push(&buffer, '\0');

	if (ASHE_UNLIKELY(c == '\0' && dq)) {
		a_arr_char_free(&buffer, NULL);
		token.u.error = "expected '\"', instead got 'EOL'";
		token.type = TK_ERROR;
	}

	ptr = a_arr_ptr(buffer);
	if (*ptr != '=' && (ptr = strstr(ptr, "=")) != NULL) {
		*ptr = '\0';
		klen = strlen(a_arr_ptr(buffer));
		if (strspn(a_arr_ptr(buffer), ENV_VAR_CHARS) == klen)
			token.type = TK_KVPAIR;
		*ptr = '=';
	}

	ashe_escape(&buffer);

	if (buffer.len == 2 && a_arr_ptr(buffer)[0] == '-') {
		a_arr_char_free(&buffer, NULL);
		token.type = TK_MINUS;
	} else {
		n = 0;
		code = all_chars_are_digits(a_arr_ptr(buffer), &n);
		if (code != -1 && code != -2) {
			token.u.number = n;
			token.type = TK_NUMBER;
		} else {
			token.u.string = buffer;
		}
		a_arr_ccharp_push(&ashe.sh_strings, a_arr_ptr(buffer));
	}
	return token;
}

/* Skip whitespace characters and comments */
ASHE_PRIVATE void skipws(struct a_lexer *lexer)
{
	a_int32 c;

	c = peek(lexer, 0);
	for (;;) {
		switch (c) {
		case '#':
			advance(lexer);
			while ((c = peek(lexer, 0)) != '\n' && c != '\v')
				advance(lexer);
			break;
		case '\n':
		case '\r':
		case ' ':
		case '\t':
		case '\v':
			advance(lexer);
			c = peek(lexer, 0);
			break;
		default:
			return;
		}
	}
}

ASHE_PRIVATE inline struct a_token a_token_new(enum a_toktype type, const char *start)
{
	struct a_token token;
	token.type = type;
	token.start = start;
	token.end = ashe.sh_lexer.current;
	return token;
}

ASHE_PUBLIC struct a_token a_lexer_next(struct a_lexer *lexer)
{
	a_int32 c;
	enum a_toktype type;
	const char *start;

	skipws(lexer);

	start = lexer->current;
	if ((c = peek(lexer, 0)) == '\0') {
		advance(lexer);
		return a_token_new(TK_EOL, start);
	}

	switch (c) {
	case '<': {
		switch (peek(lexer, 1)) {
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
		switch (peek(lexer, 1)) {
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
		switch (peek(lexer, 1)) {
		case '|':
			advance(lexer);
			type = TK_PIPE_PIPE;
			break;
		default:
			type = TK_PIPE;
			break;
		}
		break;
	}
	case '&': {
		switch (peek(lexer, 1)) {
		case '&':
			advance(lexer);
			type = TK_AND_AND;
			break;
		case '>':
			if (peek(lexer, 2) == '>') {
				advance(lexer);
				advance(lexer);
				type = TK_AND_GREATER_GREATER;
			} else {
				advance(lexer);
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
		if (!isspace(peek(lexer, 1)))
			return a_token_string(lexer);
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
		return a_token_string(lexer);
	}

	advance(lexer);
	return a_token_new(type, start);
}
