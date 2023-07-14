#include "asheutils.h"
#include "string.h"
#include "token.h"
#include <stdio.h>
#include <string.h>

token_t an_token_new(tokentype_t ttype, const byte *str)
{
    string_t *string;

    if(is_some(str)) {
        string = string_from(str);
    }

    if(is_null(string)) {
        OOM_ERR(strlen(str));
        return (token_t){0};
    }

    return (token_t){.type = ttype, .contents = string};
}
