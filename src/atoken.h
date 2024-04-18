#ifndef ATOKEN_H
#define ATOKEN_H

#include "acommon.h"
#include "aarray.h"

ARRAY_NEW(a_arr_char, char)

enum a_toktype {
	TK_AND_AND = 0, /* '&&' */
	TK_PIPE_PIPE, /* '||' */
	TK_LESS_AND, /* '<&' */
	TK_GREATER_AND, /* '>&' */
	TK_GREATER_PIPE, /* TODO: Implement... '>|' */
	TK_GREATER_GREATER, /* '>>' */
	TK_AND_GREATER, /* '&>' */
	TK_AND_GREATER_GREATER, /* '&>>' */
	TK_LESS_GREATER, /* '<>' */
	TK_LESS, /* '<' */
	TK_GREATER, /* '>' */
	TK_MINUS, /* '-' */
	TK_SEMICOLON, /* ';' */
	TK_LPAREN, /* '(' */
	TK_RPAREN, /* ')' */
	TK_PIPE, /* '|' */
	TK_AND, /* '&' */
	TK_EOL, /* '\0' */
	TK_ERROR, /* lexer error */
	TK_WORD, /* string */
	TK_KVPAIR, /* key=value */
	TK_NUMBER, /* number (integer) */
};

struct a_token {
	enum a_toktype type;
	union {
		const char *error;
		a_arr_char string;
		a_memmax number;
	} u;
	const char *start; /* debug */
	const char *end; /* debug */
};

#endif
