#include "an_string.h"
#include "an_token.h"
#include "an_utils.h"
#include <stdio.h>

an_token_t an_token_new(an_tokentype_t ttype, const byte *str)
{
    an_string_t *string = an_string_from(str);

    if (is_null(string))
    {
        fprintf(stderr, "%s:%d - out of memory\n", __FILE__, __LINE__);
        return (an_token_t){0};
    }

    return (an_token_t){.type = ttype, .contents = string};
}
