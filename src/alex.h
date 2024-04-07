#ifndef ALEXER_H
#define ALEXER_H

#include "atoken.h"

struct a_lexer {
	struct a_token curr;
	struct a_token prev;
	const char *current;
	const char *start; // for debug
};

/* global lexer */
#define A_LEX ashe.sh_lexer
/* tokens */
#define A_CTOK ashe.sh_lexer.curr
#define A_PTOK ashe.sh_lexer.prev
/* current token number */
#define A_CTOK_NUM() (A_CTOK.u.number)
/* previous token number */
#define A_PTOK_NUM() (A_PTOK.u.number);
/* current token cstring */
#define A_CTOK_STR() (A_CTOK.u.string.data)
/* previous token cstring */
#define A_PTOK_STR() (A_PTOK.u.string.data)

void a_lexer_init(struct a_lexer *lexer, const char *start);
struct a_token a_lexer_next(struct a_lexer *lexer);

#endif
