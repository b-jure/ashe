#include "an_string.h"
#include "an_vec.h"
#include <stdlib.h>
#include <string.h>
#define NULL_TERM '\0'

struct an_string_t
{
    an_vec_t *vec;
};

an_string_t *an_string_new(void)
{
    an_string_t *string = malloc(sizeof(an_string_t));

    if (is_null(string) ||
        is_null(string->vec = an_vec_with_capacity(sizeof(byte), 1)))
    {
        return NULL;
    }
    else if (!an_vec_push(string->vec, &(char){NULL_TERM}))
    {
        an_string_drop(&string);
        return NULL;
    }
    else
    {
        return string;
    }
}

an_string_t *an_string_with_cap(size_t capacity)
{
    an_string_t *string;

    if (capacity < 2)
    {
        return an_string_new();
    }
    else
    {
        if (is_null(string = malloc(sizeof(an_string_t))))
        {
            return NULL;
        }
        else
        {
            string->vec = an_vec_with_capacity(sizeof(char), capacity);

            if (is_null(string->vec) ||
                !an_vec_push(string->vec, &(char){NULL_TERM}))
            {
                an_string_drop(&string);
                return NULL;
            }
        }

        return string;
    }
}

an_string_t *an_string_from(const byte *str)
{
    size_t str_len = strlen(str);
    an_string_t *string = an_string_new();

    if (is_null(string) || !an_vec_splice(string->vec, 0, 0, str, str_len))
    {
        return NULL;
    }

    return string;
}

const byte *an_string_ref(an_string_t *string)
{
    return an_vec_front(string->vec);
}

byte *an_string_slice(an_string_t *string, size_t index)
{
    if (is_null(string))
    {
        return NULL;
    }

    return an_vec_index(string->vec, index);
}

bool an_string_splice(an_string_t *self,
                      size_t index,
                      size_t remove_n,
                      const byte *str,
                      size_t insert_n)
{
    if (is_null(self))
    {
        return false;
    }

    return an_vec_splice(self->vec, index, remove_n, str, insert_n);
}

bool an_string_eq(an_string_t *self, an_string_t *other)
{
    return an_vec_eq(self->vec, other->vec, NULL);
}

bool an_string_append(an_string_t *self, const void *str, size_t len)
{
    if (is_null(self))
    {
        return false;
    }

    return an_vec_append(self->vec, str, len);
}

bool an_string_set(an_string_t *self, const byte ch, size_t index)
{
    size_t len;

    if (is_null(self) || index > (len = an_string_len(self)))
    {
        return false;
    }

    if (index == len && ch != '\0')
    {
        /* Null terminate the string */
        if (!an_vec_push(self->vec, 0))
        {
            return false;
        }
    }

    return an_vec_set(self->vec, &ch, index);
}

int an_string_get(an_string_t *self, size_t index)
{
    if (is_null(self))
    {
        return -1;
    }

    byte out;
    an_vec_get(self->vec, index, &out);
    return out;
}

size_t an_string_len(an_string_t *self)
{
    if (is_null(self))
    {
        return 0;
    }

    return an_vec_len(self->vec) - 1; /* Do not count null terminator */
}

void an_string_drop(an_string_t **self)
{
    if (is_some(self) && is_some(*self))
    {
        an_vec_drop(&(*self)->vec, NULL);
        free(*self);
        *self = NULL;
    }
}
