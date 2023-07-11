#ifndef __AN_LEXER_H__
#define __AN_LEXER_H__

#include "an_token.h"
#include "an_utils.h"

typedef struct an_lexer_t an_lexer_t;

an_lexer_t an_lexer_new(byte *start, size_t len);

an_token_t an_lexer_next(an_lexer_t *lexer);

#endif
