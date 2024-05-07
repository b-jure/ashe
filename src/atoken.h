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
