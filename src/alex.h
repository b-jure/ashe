#ifndef ALEXER_H
#define ALEXER_H

#include "atoken.h"

struct a_lexer {
	struct a_token curr;
	struct a_token prev;
	ubyte ws; /* set each time whitespace is skipped */
	const char *current;
	const char *start; // for debug
};

/* current token number */
#define CNM(lexer) ((lexer)->curr.u.number)
/* previous token number */
#define PNM(lexer) ((lexer)->prev.u.number);
/* current token cstring */
#define CSTR(lexer) ((lexer)->curr.u.string.data)
/* previous token cstring */
#define PSTR(lexer) ((lexer)->prev.u.string.data)

void a_lexer_init(struct a_lexer *lexer, const char *start);
struct a_token a_lexer_next(struct a_lexer *lexer);
const char *a_token_debug(struct a_token *token);

#endif
