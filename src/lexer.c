#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define is_reserved_symbol(ch)                                                           \
    ((ch) == '&' || (ch) == '|' || (ch) == '>' || (ch) == '<' || (ch) == ';')

static token_t token_without_quotes(tokentype_t ttype, byte *word);
static void lexer_get_string(lexer_t *lexer);
static void lexer_skip_ws(lexer_t *lexer);

lexer_t lexer_new(const byte *start, size_t len)
{
    return (lexer_t){.token = {0}, .iter = chariter_new((byte *) start, len)};
}

static void lexer_get_string(lexer_t *lexer)
{
    static byte word[ARG_MAX];

    int c, i;
    byte *ptr;
    byte old;
    bool dquote = false;
    bool escape = false;
    chariter_t *iter = &lexer->iter;
    tokentype_t ttype;

    for(i = 0; ((c = chariter_peek(iter)) != EOL); i++) {
        if(!dquote && ((isspace(c)) || is_reserved_symbol(c))) {
            break;
        }

        if(!escape && c == '"') {
            dquote ^= true;
        } else if(c == '\\' || escape) {
            escape ^= true;
        }

        word[i] = chariter_next(iter);
    }

    word[i] = '\0';
    ttype = WORD_TOKEN;

    if((ptr = strstr(word, "=")) != NULL && word != ptr) {
        old = *ptr;
        *ptr = NULL_TERM;
        if(strspn(word, PORTABLE_CHARACTER_SET) == strlen(word))
            ttype = KVPAIR_TOKEN;
        *ptr = old;
    }

    lexer->token = token_without_quotes(ttype, word);
}

static token_t token_without_quotes(tokentype_t ttype, byte *word)
{
    token_t token;
    string_t *string = string_from(word);
    byte *ptr = string_slice(string, 0);

    if(__glibc_unlikely(is_null(string)))
        return token_new(OOM_TOKEN, NULL);

    token.type = ttype;
    token.contents = string;

    while(is_some(ptr = strchr(ptr, '"'))) {
        if(char_before_ptr(ptr) != '\\')
            string_remove_at_ptr(string, ptr);
        else
            ptr++;
    }

    return token;
}

static void lexer_skip_ws(lexer_t *lexer)
{
    int c;
    chariter_t *iter = &lexer->iter;
    while(isspace((c = chariter_peek(iter))))
        chariter_next(iter);
}

static void print_token(token_t *token)
{
    switch(token->type) {
        case REDIROP_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "REDIRECTION", string_ref(token->contents));
            break;
        case WORD_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "WORD", string_ref(token->contents));
            break;
        case KVPAIR_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "K/V PAIR", string_ref(token->contents));
            break;
        case PIPE_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "PIPE", string_ref(token->contents));
            break;
        case AND_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "AND", string_ref(token->contents));
            break;
        case OR_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "OR", string_ref(token->contents));
            break;
        case BG_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "BACKGROUND", string_ref(token->contents));
            break;
        case FG_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "FOREGROUND", string_ref(token->contents));
            break;
        case NAT_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "NAT", "NULL");
            break;
        case EOL_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "EOL", "NULL");
            break;
        case OOM_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "OOM", "NULL");
            break;
    }
}

token_t lexer_next(lexer_t *lexer)
{
    int c;
    chariter_t *iter = &lexer->iter;

    lexer_skip_ws(lexer);

    switch((c = chariter_peek(iter))) {
        case '2':
            chariter_next(iter);
            if(chariter_peek(iter) == '>') {
                if(*(iter->next + 1) == '>') {
                    chariter_next(iter);
                    lexer->token = token_new(REDIROP_TOKEN, "2>>");
                } else {
                    lexer->token = token_new(REDIROP_TOKEN, "2>");
                }
            } else {
                lexer->token = token_new(NAT_TOKEN, NULL);
            }
            break;
        case '>':
            if(*(iter->next + 1) == '>') {
                chariter_next(iter);
                lexer->token = token_new(REDIROP_TOKEN, ">>");
            } else {
                lexer->token = token_new(REDIROP_TOKEN, ">");
            }
            break;
        case '<':
            lexer->token = token_new(REDIROP_TOKEN, "<");
            break;
        case '&':
            if(*(iter->next + 1) == '&') {
                chariter_next(iter);
                lexer->token = token_new(AND_TOKEN, "&&");
            } else {
                lexer->token = token_new(BG_TOKEN, "&");
            }
            break;
        case '|':
            if(*(iter->next + 1) == '|') {
                chariter_next(iter);
                lexer->token = token_new(OR_TOKEN, "||");
            } else {
                lexer->token = token_new(PIPE_TOKEN, "|");
            }
            break;
        case ';':
            lexer->token = token_new(FG_TOKEN, ";");
            break;
        case '\0':
            lexer->token = token_new(EOL_TOKEN, NULL);
            break;
        default:
            lexer_get_string(lexer);
            print_token(&lexer->token);
            return lexer->token;
            break;
    }

    chariter_next(iter);
    print_token(&lexer->token);
    return lexer->token;
}

token_t lexer_peek(lexer_t *lexer)
{
    return lexer->token;
}
