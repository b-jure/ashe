#ifndef ALEXER_H
#define ALEXER_H

#include "atoken.h"

typedef struct {
    Token curr;
    Token prev;
    ubyte ws; /* set each time whitespace is skipped */
    const char* current;
    const char* start; // for debug
} Lexer;


/* current token number */
#define CNM(lexer) ((lexer)->curr.u.number)
/* previous token number */
#define PNM(lexer) ((lexer)->prev.u.number);
/* current token cstring */
#define CSTR(lexer) ((lexer)->curr.u.string.data)
/* previous token cstring */
#define PSTR(lexer) ((lexer)->prev.u.string.data)


void Lexer_init(Lexer* lexer, const char* start);
void Lexer_free(Lexer* lexer);
Token Lexer_next(Lexer* lexer);
const char* Token_tostr(Token* token);

#endif
