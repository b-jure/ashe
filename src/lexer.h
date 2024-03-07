#ifndef ALEXER_H
#define ALEXER_H

#include "token.h"

typedef struct {
    Token curr;
    Token prev;
    const char* current;
    const char* start; // for debug
} Lexer;

void Lexer_init(Lexer* lexer, const char* start);
Token Lexer_next(Lexer* lexer);

const char* Token_tostr(Token* token);

#endif
