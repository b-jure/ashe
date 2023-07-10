#include "an_chiter.h"
#include <stdio.h>
#define is_exhausted(iter) ((iter)->next == (iter)->end || (iter)->next == NULL)

an_chariter_t an_chariter_new(byte *start, size_t len)
{
    return (an_chariter_t){.next = start, .end = start + len};
}

an_chariter_t an_chariter_from_string(an_string_t *string)
{
    if (is_null(string))
    {
        return an_chariter_new(0, 0);
    }
    else
    {
        return an_chariter_new(an_string_slice(string, 0), an_string_len(string));
    }
}

int an_chariter_next(an_chariter_t *iter)
{
    if (is_null(iter) || is_exhausted(iter))
    {
        return EOL;
    }

    return *iter->next++;
}

int an_chariter_peek(an_chariter_t *iter)
{
    if (is_null(iter) || is_exhausted(iter))
    {
        return EOL;
    }

    return *iter->next;
}
