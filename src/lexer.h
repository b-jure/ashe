#ifndef __ASH_LEXER_H__
#define __ASH_LEXER_H__

#include "asheutils.h"
#include "chiter.h"
#include "token.h"

typedef struct {
  token_t token;
  chariter_t iter;
} lexer_t;

lexer_t lexer_new(const byte *start, size_t len);

token_t lexer_next(lexer_t *lexer);

token_t lexer_peek(lexer_t *lexer);

#endif
