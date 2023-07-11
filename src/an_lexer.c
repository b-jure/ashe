#include "an_chiter.h"
#include "an_lexer.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#define MAX_NAME NAME_MAX

#if defined(__unix__) || defined(__linux__)
#define PATH_DELIM ':'
#elif defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
#define PATH_DELIM ';'
#endif

struct an_lexer_t
{
    an_token_t token;
    an_chariter_t iter;
};

an_lexer_t an_lexer_new(byte *start, size_t len)
{
    return (an_lexer_t){.token = {0}, .iter = an_chariter_new(start, len)};
}

static void an_lexer_get_word(an_lexer_t *lexer)
{
    static byte word[MAX_NAME];

    int c, i;
    an_chariter_t *iter = &lexer->iter;

    for (i = 0; i < (MAX_NAME - 1) && !isspace(an_chariter_peek(iter)); i++)
    {
        word[i] = an_chariter_next(iter);
    }

    if (i >= MAX_NAME)
    {
        fprintf(stderr, "%s:%d - maximum word size exceeded\n", __FILE__, __LINE__);
        lexer->token = an_token_new(NAT_TOKEN, "");
    }
    else
    {
        word[i] = '\0';
        lexer->token = an_token_new(WORD_TOKEN, word);
    }
}

static void an_lexer_skip_ws(an_lexer_t *lexer)
{
    int c;
    an_chariter_t *iter = &lexer->iter;

    while (isspace((c = an_chariter_peek(iter))))
        an_chariter_next(iter);
}

an_token_t an_lexer_next(an_lexer_t *lexer)
{
    int c;
    byte *identifier;
    an_chariter_t *iter = &lexer->iter;

    an_lexer_skip_ws(lexer);

    switch ((c = an_chariter_peek(iter)))
    {
        case '2':
            an_chariter_next(iter);
            if (an_chariter_peek(iter) == '>')
            {
                lexer->token = an_token_new(REDIROP_TOKEN, "2>");
            }
            else
            {
                lexer->token = an_token_new(NAT_TOKEN, "");
            }
            break;
        case '>':
            an_chariter_next(iter);
            if (an_chariter_peek(iter) == '>')
            {
                lexer->token = an_token_new(REDIROP_TOKEN, ">>");
            }
            else
            {
                an_chariter_goback_unsafe(iter, 1);
                lexer->token = an_token_new(REDIROP_TOKEN, ">");
            }
            break;
        case '<':
            lexer->token = an_token_new(REDIROP_TOKEN, "<");
            break;
        case '&':
            an_chariter_next(iter);
            if (an_chariter_peek(iter) == '&')
            {
                lexer->token = an_token_new(AND_TOKEN, "&&");
            }
            else
            {
                an_chariter_goback_unsafe(iter, 1);
                lexer->token = an_token_new(BG_TOKEN, "&");
            }
            break;
        case '|':
            an_chariter_next(iter);
            if (an_chariter_peek(iter) == '|')
            {
                lexer->token = an_token_new(OR_TOKEN, "||");
            }
            else
            {
                an_chariter_goback_unsafe(iter, 1);
                lexer->token = an_token_new(PIPE_TOKEN, "|");
            }
            break;
        case ';':
            lexer->token = an_token_new(FG_TOKEN, ";");
            break;
        case EOL:
            lexer->token = an_token_new(EOL_TOKEN, "");
            break;
        default:
            an_lexer_get_word(lexer);
            break;
    }

    an_chariter_next(iter);
    return lexer->token;
}
