#include "an_string.h"
#include "an_token.h"
#include "an_utils.h"
#include <stdio.h>
#include <string.h>

an_token_t an_token_new(an_tokentype_t ttype, const byte *str)
{
    an_string_t *string;

    if(is_some(str)) {
        string = an_string_from(str);
    }

    if(is_null(string)) {
        OOM_ERR(strlen(str));
        return (an_token_t){0};
    }

    return (an_token_t){.type = ttype, .contents = string};
}
