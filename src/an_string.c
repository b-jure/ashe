#include "an_string.h"
#include "an_utils.h"
#include "an_vec.h"
#include <stdlib.h>

struct an_string_t {
    an_vec_t* vec;
};

an_string_t* an_string_new(void)
{
    an_string_t* string = string = malloc(sizeof(an_string_t));
    if(is_null(string) || is_null(string->vec = an_vec_new(sizeof(char)))) { return NULL; }
    return string;
}

an_string_t* an_string_from_array(char array[])
{
    an_string_t* string = an_string_new();

    if(is_null(string)) {
        return NULL;
    }
    // TODO
    
}
