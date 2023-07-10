#include "an_chiter.h"
#include "an_lexer.h"
#include <limits.h>
#include <sys/param.h>

#if defined(__unix__) || defined(__linux__)
#define PATH_DELIM ':'
#elif defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
#define PATH_DELIM ';'
#endif
#define MAX_PATH _POSIX_ARG_MAX

struct an_lexer_t
{
    an_token_t token;
    an_chariter_t iter;
};

an_lexer_t an_lexer_new(byte *start, size_t len)
{
    return (an_lexer_t){.token = {0}, .iter = an_chariter_new(start, len)};
}

an_token_t an_lexer_next(an_lexer_t *lexer)
{
    an_chariter_t *iterator = &lexer->iter;
    int c = an_chariter_peek(iterator);

    switch (c)
    {
    case '|':
        return an_token_new(PIPE_TOKEN, "|");
    case '>':
        if (an_chariter_next(iterator) == '>')
        {
            return an_token_new(REDIR_O_APN_TOKEN, ">>");
        }
        else
        {
            return an_token_new(REDIR_O_CRT_TOKEN, ">");
        }
        break;
    case '<':
        return an_token_new(REDIR_I_TOKEN, "<");
    case EOL:
        return an_token_new(EOL_TOKEN, "");
    default:
        return an_token_new(NAT_TOKEN, NULL);
    }
}
