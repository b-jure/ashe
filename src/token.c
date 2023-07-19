#include "ashe_string.h"
#include "ashe_utils.h"
#include "token.h"
#include <stdio.h>
#include <string.h>

token_t token_new(tokentype_t ttype, const byte *str)
{
    string_t *string = NULL;

    if(is_some(str)) {
        string = string_from(str);
        if(is_null(string)) {
            pwarn("ran out of memory trying to allocate '%ld' bytes", strlen(str));
            return (token_t){.type = OOM_TOKEN, .contents = NULL};
        }
    }

    return (token_t){.type = ttype, .contents = string};
}
