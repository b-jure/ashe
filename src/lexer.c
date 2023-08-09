#include "ashe_utils.h"
#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define iter_peek_next(iter) ((iter)->next + 1)

#define is_reserved_symbol(ch) ((ch) == '&' || (ch) == '|' || (ch) == '>' || (ch) == '<' || (ch) == ';')

static token_t token_without_quotes(tokentype_t ttype, byte* word);
static void    lexer_get_string(lexer_t* lexer);
static void    lexer_skip_ws(lexer_t* lexer);

lexer_t lexer_new(const byte* start, size_t len)
{
    return (lexer_t){.token = {0}, .iter = chariter_new((byte*) start, len)};
}

static void expand_vars(byte** word)
{
    size_t      len   = strlen(*word);
    byte*       ptr   = *word;
    byte*       start = NULL;
    byte*       end   = NULL;
    const byte* value = NULL;

    for(; is_some((ptr = strchr(ptr, '$'))); ptr++) {
        if(is_escaped(ptr, ptr - *word)) {
            continue;
        }

        size_t offset = strspn(ptr + 1, PORTABLE_CHARACTER_SET);

        if(offset == 0 || __glibc_unlikely((end = (start = ptr + 1) + offset) - *word >= ARG_MAX)) {
            continue;
        }

        byte cached     = *end;
        *end            = NULL_TERM;
        int32_t key_len = strlen(start) + 1; // Account for '$'
        *end            = cached;

        value = getenv(start);

        if(is_some(value)) {
            int32_t var_len = strlen(value);
            int32_t diff    = var_len - key_len;

            if(diff > 0) {
                if(__glibc_likely(len + diff < ARG_MAX)) {
                    memcpy(end, end + diff, diff);
                } else {
                    continue;
                }
            } else {
                diff = abs(diff);
                memcpy(end - diff, end, diff);
            }

            memcpy(ptr, value, var_len);
        } else {
            /* If no variable found remove the whole key indicated with '$' */
            memcpy(ptr, end, key_len);
        }
    }
}

static void lexer_get_string(lexer_t* lexer)
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
        if(!dquote && !escape && ((isspace(c)) || is_reserved_symbol(c))) {
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

    ptr = word;
    expand_vars(&ptr);

    if((ptr = strstr(word, "=")) != NULL && word != ptr) {
        old  = *ptr;
        *ptr = NULL_TERM;
        if(strspn(word, PORTABLE_CHARACTER_SET) == strlen(word)) {
            ttype = KVPAIR_TOKEN;
        }
        *ptr = old;
    }

    lexer->token = token_without_quotes(ttype, word);
}

static token_t token_without_quotes(tokentype_t ttype, byte* word)
{
    token_t   token;
    string_t* string = string_from(word);
    byte*     ptr    = string_slice(string, 0);

    if(__glibc_unlikely(is_null(string)))
        return token_new(OOM_TOKEN, NULL);

    token.type     = ttype;
    token.contents = string;

    while(is_some(ptr = strchr(ptr, '"'))) {
        if(char_before_ptr(ptr) != '\\')
            string_remove_at_ptr(string, ptr);
        else
            ptr++;
    }

    return token;
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
