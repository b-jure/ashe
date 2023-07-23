#include "ashe_string.h"
#include "vec.h"
#include <stdlib.h>
#include <string.h>

struct string_t {
    vec_t *vec;
};

string_t *string_new(void)
{
    string_t *string = malloc(sizeof(string_t));
    if(__glibc_unlikely(is_null(string))) {
        pwarn("ran out of memory trying to allocate '%ld' bytes", sizeof(string_t));
    }

    if(__glibc_unlikely(is_null(string->vec = vec_with_capacity(sizeof(byte), 1)))) {
        return NULL;
    } else if(__glibc_unlikely(!vec_push(string->vec, &(char){NULL_TERM}))) {
        string_drop(string);
        return NULL;
    } else {
        return string;
    }
}

string_t *string_with_cap(size_t capacity)
{
    string_t *string;

    if(capacity < 2) {
        return string_new();
    } else {
        if(__glibc_unlikely(is_null(string = malloc(sizeof(string_t))))) {
            pwarn("ran out of memory trying to allocate '%ld' bytes", sizeof(string_t));
            return NULL;
        } else {
            string->vec = vec_with_capacity(sizeof(char), capacity);

            if(__glibc_unlikely(is_null(string->vec))
               || __glibc_unlikely(!vec_push(string->vec, &(char){NULL_TERM})))
            {
                string_drop(string);
                return NULL;
            }
        }

        return string;
    }
}

bool string_remove(string_t *self, size_t index)
{
    if(is_null(self)) {
        return false;
    }

    return vec_remove(self->vec, index, NULL);
}

int string_last(string_t *self)
{
    if(is_null(self)) {
        return EOF;
    } else if(vec_len(self->vec) == 0) {
        return NULL_TERM;
    } else {
        return *((byte *) vec_back(self->vec) - 1);
    }
}

int string_first(string_t *self)
{
    if(is_null(self)) {
        return EOF;
    } else if(vec_len(self->vec) == 0) {
        return NULL_TERM;
    } else {
        return *(int *) vec_front(self->vec);
    }
}

bool string_remove_at_ptr(string_t *self, byte *ptr)
{
    if(is_null(self)) {
        return false;
    }

    return vec_remove_at_ptr(self->vec, ptr, NULL);
}

bool string_set_at_ptr(string_t *self, byte *ptr, char c)
{
    if(is_null(self)) {
        return false;
    }

    return vec_set_at_ptr(self->vec, ptr, &c);
}

string_t *string_from(const byte *str)
{
    size_t str_len = strlen(str);
    string_t *string = string_new();

    if(__glibc_unlikely(is_null(string))
       || __glibc_unlikely(!vec_splice(string->vec, 0, 0, str, str_len)))
    {
        return NULL;
    }

    return string;
}

const byte *string_ref(string_t *string)
{
    return vec_front(string->vec);
}

byte *string_slice(string_t *string, size_t index)
{
    if(is_null(string)) {
        return NULL;
    }

    return vec_index(string->vec, index);
}

bool string_splice(
    string_t *self,
    size_t index,
    size_t remove_n,
    const byte *str,
    size_t insert_n)
{
    if(is_null(self)) {
        return false;
    }

    return vec_splice(self->vec, index, remove_n, str, insert_n);
}

bool string_eq(string_t *self, string_t *other)
{
    return vec_eq(self->vec, other->vec, NULL);
}

bool string_append(string_t *self, const void *str, size_t len)
{
    if(is_null(self)) {
        return false;
    }

    return vec_splice(self->vec, string_len(self), 0, str, len);
}

bool string_push(string_t *self, byte c)
{
    if(is_some(self)) {
        return vec_push(self->vec, &c);
    }
    return false;
}

bool string_clear(string_t *self)
{
    if(is_some(self)) {
        vec_clear(self->vec, NULL);
        return string_push(self, NULL_TERM);
    }
    return false;
}

bool string_set(string_t *self, const byte ch, size_t index)
{
    size_t len;

    if(is_null(self) || index > (len = string_len(self))) {
        return false;
    }

    if(index == len && ch != '\0') {
        /* Null terminate the string */
        if(!vec_push(self->vec, 0)) {
            return false;
        }
    }

    return vec_set(self->vec, &ch, index);
}

int string_get(string_t *self, size_t index)
{
    if(is_null(self)) {
        return -1;
    }

    byte out;
    vec_get(self->vec, index, &out);
    return out;
}

size_t string_len(string_t *self)
{
    if(is_null(self)) {
        return 0;
    }

    return vec_len(self->vec) - 1; /* Do not count null terminator */
}

void string_drop_inner(string_t *self)
{
    if(is_some(self) && is_some(self->vec)) {
        vec_clear_capacity(self->vec, NULL);
    }
}

void string_drop(string_t *self)
{
    if(is_some(self)) {
        vec_drop(&self->vec, NULL);
        free(self);
    }
}
