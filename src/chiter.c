#include "chiter.h"
#include <stdio.h>
#define is_exhausted(iter) ((iter)->next >= (iter)->end || (iter)->next == NULL)

chariter_t chariter_new(byte *start, size_t len)
{
    return (chariter_t){.next = start, .end = start + len};
}

chariter_t chariter_from_string(string_t *string)
{
    if(is_null(string)) {
        return chariter_new(0, 0);
    } else {
        return chariter_new(string_slice(string, 0), string_len(string));
    }
}

int chariter_next(chariter_t *iter)
{
    if(is_null(iter) || is_exhausted(iter)) {
        return EOL;
    }

    return *iter->next++;
}

int chariter_goback_unsafe(chariter_t *iter, size_t steps)
{
    if(is_null(iter) || is_exhausted(iter)) {
        return -1;
    }

    while(steps--) {
        iter->next--;
    }

    return 0;
}

bool chariter_set_unsafe(chariter_t *iter, byte *next)
{
    if(next > iter->end) {
        return false;
    }

    iter->next = next;
    return true;
}

int chariter_peek(chariter_t *iter)
{
    if(is_null(iter) || is_exhausted(iter)) {
        return EOL;
    }

    return *(iter->next);
}
