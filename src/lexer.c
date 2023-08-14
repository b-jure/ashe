#include "ashe_utils.h"
#include "errors.h"
#include "lexer.h"

#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>

#define iter_peek_next(iter) ((iter)->next + 1)

// TODO: Implement ****GLOB OPERATOR****, and rest of the regular expressions

static void lexer_get_string(lexer_t* lexer);
static void lexer_skip_ws(lexer_t* lexer);
static bool is_reserved(byte c);
static void plexerr(uint err);

static bool is_reserved(byte c)
{
    static bool reserved_table[256] = {false};

    reserved_table['&'] = true;
    reserved_table['|'] = true;
    reserved_table['>'] = true;
    reserved_table['<'] = true;
    reserved_table[';'] = true;
    reserved_table['['] = true;
    reserved_table[']'] = true;
    reserved_table['?'] = true;
    reserved_table['*'] = true;
    reserved_table['!'] = true;
    reserved_table['{'] = true;
    reserved_table['}'] = true;

    return reserved_table[(unsigned char) c];
}

// TODO: Make use of lexer errors when regular expression handling gets implemented
#define LE_EOSBRM 0
#define LE_EOSPEX 1
#define LE_PARAME 2
__attribute__((unused)) static void plexerr(uint err)
{
    static byte* lexer_err_table[] = {
        "Unexpected end of string, square brackets do not match",
        "Unexpected end of string, incomplete parameter expansion",
        "Unexpected '}' for unopened brace expansion",
    };

    pwarn("%s", lexer_err_table[err]);
}

lexer_t lexer_new(const byte* start, size_t len)
{
    return (lexer_t){.token = {0}, .iter = chariter_new((byte*) start, len)};
}

void lexer_get_string(lexer_t* lexer)
{
    static byte word[ARG_MAX];

    int         c, i;
    byte*       ptr;
    byte        old;
    bool        dquote = false;
    bool        escape = false;
    chariter_t* iter   = &lexer->iter;
    tokentype_t ttype;

    for(i = 0; ((c = chariter_peek(iter)) != EOL); i++) {
        if(!dquote && !escape && ((isspace(c)) || is_reserved(c))) {
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
    ttype   = WORD_TOKEN;
    ptr     = word;

    expand_vars(&ptr);

    if((ptr = strstr(word, "=")) != NULL && word != ptr) {
        old  = *ptr;
        *ptr = NULL_TERM;
        if(strspn(word, PORTABLE_CHARACTER_SET) == strlen(word)) {
            ttype = KVPAIR_TOKEN;
        }
        *ptr = old;
    }

    unescape(word);
    lexer->token = token_new(ttype, word);
}

static void lexer_skip_ws(lexer_t* lexer)
{
    int         c;
    chariter_t* iter = &lexer->iter;
    while(isspace((c = chariter_peek(iter)))) chariter_next(iter);
}

/// Debug
__attribute__((unused)) void print_token(token_t* token)
{
    switch(token->type) {
        case REDIROP_TOKEN: fprintf(stderr, "REDIROP : '%s'\n", string_ref(token->contents)); break;
        case WORD_TOKEN: fprintf(stderr, "WORD : '%s'\n", string_ref(token->contents)); break;
        case KVPAIR_TOKEN: fprintf(stderr, "KVPAIR : '%s'\n", string_ref(token->contents)); break;
        case PIPE_TOKEN: fprintf(stderr, "PIPE : '%s'\n", string_ref(token->contents)); break;
        case AND_TOKEN: fprintf(stderr, "AND : '%s'\n", string_ref(token->contents)); break;
        case OR_TOKEN: fprintf(stderr, "OR : '%s'\n", string_ref(token->contents)); break;
        case BG_TOKEN: fprintf(stderr, "BG : '%s'\n", string_ref(token->contents)); break;
        case FG_TOKEN: fprintf(stderr, "FG : '%s'\n", string_ref(token->contents)); break;
        case EOL_TOKEN: fprintf(stderr, "EOL : 'NULL'\n"); break;
        case OOM_TOKEN: fprintf(stderr, "OOM : 'NULL'\n"); break;
        case SYNTAX_ERR_TOKEN: fprintf(stderr, "SYNTAX ERR : 'NULL'\n"); break;
    }
}

token_t lexer_next(lexer_t* lexer)
{
    int         c;
    chariter_t* iter = &lexer->iter;

    lexer_skip_ws(lexer);

    switch((c = chariter_peek(iter))) {
        case '2':
            if(*(iter_peek_next(iter)) == '>') {
                chariter_next(iter);
                if(*(iter_peek_next(iter)) == '>') {
                    chariter_next(iter);
                    lexer->token = token_new(REDIROP_TOKEN, "2>>");
                } else {
                    lexer->token = token_new(REDIROP_TOKEN, "2>");
                }
            } else {
                lexer_get_string(lexer);
                return lexer->token;
            }
            break;
        case '>':
            if(*(iter_peek_next(iter)) == '>') {
                chariter_next(iter);
                lexer->token = token_new(REDIROP_TOKEN, ">>");
            } else {
                lexer->token = token_new(REDIROP_TOKEN, ">");
            }
            break;
        case '<': lexer->token = token_new(REDIROP_TOKEN, "<"); break;
        case '&':
            if(*(iter_peek_next(iter)) == '&') {
                chariter_next(iter);
                lexer->token = token_new(AND_TOKEN, "&&");
            } else {
                lexer->token = token_new(BG_TOKEN, "&");
            }
            break;
        case '|':
            if(*(iter_peek_next(iter)) == '|') {
                chariter_next(iter);
                lexer->token = token_new(OR_TOKEN, "||");
            } else {
                lexer->token = token_new(PIPE_TOKEN, "|");
            }
            break;
        case ';': lexer->token = token_new(FG_TOKEN, ";"); break;
        case '\0': lexer->token = token_new(EOL_TOKEN, NULL); break;
        default: lexer_get_string(lexer); return lexer->token;
    }

    chariter_next(iter);
    return lexer->token;
}

token_t lexer_peek(lexer_t* lexer)
{
    return lexer->token;
}
