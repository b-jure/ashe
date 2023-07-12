#ifndef __AN_LEXER_H__
#define __AN_LEXER_H__

#include "an_chiter.h"
#include "an_token.h"
#include "an_utils.h"

typedef struct lexer_t lexer_t;

struct lexer_t {
  token_t token;
  chariter_t iter;
};

lexer_t lexer_new(const byte *start, size_t len);

token_t lexer_next(lexer_t *lexer);

token_t lexer_peek(lexer_t *lexer);

#endif
