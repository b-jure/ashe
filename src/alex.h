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

#ifndef ALEXER_H
#define ALEXER_H

#include "atoken.h"

struct a_lexer {
	struct a_token curr;
	struct a_token prev;
	const char *current; /* debug */
	const char *start; /* debug */
};

/* global lexer */
#define A_LEX ashe.sh_lexer
/* tokens */
#define A_CTOK ashe.sh_lexer.curr
#define A_PTOK ashe.sh_lexer.prev
/* current token number */
#define A_CTOK_NUM() (A_CTOK.u.number)
/* previous token number */
#define A_PTOK_NUM() (A_PTOK.u.number)
/* current token cstring */
#define A_CTOK_STR() (A_CTOK.u.string.data)
/* previous token cstring */
#define A_PTOK_STR() (A_PTOK.u.string.data)

void a_lexer_init(struct a_lexer *lexer, const char *start);
struct a_token a_lexer_next(struct a_lexer *lexer);

#endif
