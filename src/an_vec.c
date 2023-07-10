#include "an_utils.h"
#include "an_vec.h"
#include <limits.h>
#include <memory.h>
#include <stdlib.h>

#define MAX_VEC_SIZE UINT_MAX

static void *_an_vec_get(const an_vec_t *self, const size_t index);
static int _an_vec_ensure(an_vec_t *self, size_t bytes);

struct _an_vec_t
{
    byte *allocation;
    size_t ele_size;
    size_t capacity;
    size_t len;
};

an_vec_t *an_vec_new(const size_t ele_size)
{
    an_vec_t *vec = malloc(sizeof(an_vec_t));

    if (is_null(vec))
    {
        return NULL;
    }

    vec->ele_size = ele_size;
    vec->capacity = 0;
    vec->len = 0;
    vec->allocation = NULL;

    return vec;
}

an_vec_t *an_vec_with_capacity(const size_t ele_size, const size_t capacity)
{
    an_vec_t *vec;
    void *allocation;

    if (is_null(vec = an_vec_new(ele_size)) ||
        is_null(allocation = calloc(capacity, ele_size)))
    {
        return NULL;
    }

    vec->allocation = allocation;
    vec->capacity = capacity;

    return vec;
}

size_t an_vec_max_size(void)
{
    return MAX_VEC_SIZE;
}

bool an_vec_splice(an_vec_t *self,
                   size_t index,
                   size_t remove_n,
                   const void *array,
                   size_t insert_n)
{
    if (is_null(self) || is_null(array) || self->len < index + remove_n ||
        (_an_vec_ensure(self, insert_n - remove_n) == -1))
    {
        return false;
    }
    /* Make space for new elements. */
    memcpy(_an_vec_get(self, index + insert_n),
           _an_vec_get(self, index + remove_n),
           self->len - (index + remove_n));
    /* Copy over the 'insert_n' elements from 'array'. */
    memcpy(_an_vec_get(self, index), array, self->ele_size * insert_n);
    self->len += (insert_n - remove_n);
    return true;
}

static int _an_vec_ensure(an_vec_t *self, size_t bytes)
{
    size_t required = self->len + bytes;

    if (required > self->capacity)
    {
        if (required > MAX_VEC_SIZE)
        {
            fprintf(stderr, "%s:%d - allocation too big\n", __FILE__, __LINE__);
            return -1;
        }
        else if (required > (MAX_VEC_SIZE / 2))
        {
            required = MAX_VEC_SIZE;
        }
        else
        {
            required *= 2;
        }

        void *new_alloc = realloc(self->allocation, required);

        if (is_null(new_alloc))
        {
            fprintf(stderr, "%s:%d - out of memory\n", __FILE__, __LINE__);
            return -1;
        }

        self->allocation = new_alloc;
        self->capacity = required;
    }

    return 0;
}

static void *_an_vec_get(const an_vec_t *self, const size_t index)
{
    return (self->allocation + (index * self->ele_size));
}

void *an_vec_front(const an_vec_t *self)
{
    if (is_null(self) || self->len == 0)
    {
        return NULL;
    }

    return _an_vec_get(self, 0);
}

void *an_vec_inner_unsafe(const an_vec_t *self)
{
    if (is_null(self))
    {
        return NULL;
    }
    return self->allocation;
}

void *an_vec_back(const an_vec_t *self)
{
    if (is_null(self) || self->len == 0)
    {
        return NULL;
    }

    return _an_vec_get(self, self->len - 1);
}

bool an_vec_push(an_vec_t *self, const void *element)
{
    if (is_null(self) || _an_vec_ensure(self, 1) == -1)
    {
        return false;
    }

    memmove(_an_vec_get(self, self->len), element, self->ele_size);
    self->len++;

    return true;
}

void an_vec_clear(an_vec_t *self, FreeFn free_fn)
{
    size_t len;

    if (is_null(self) || is_null(self->allocation))
    {
        return;
    }

    if (free_fn)
    {
        for (len = self->len; len--;)
        {
            free_fn(_an_vec_get(self, len));
        }
    }

    free(self->allocation);
    self->allocation = NULL;
    self->capacity = 0;
    self->len = 0;
}

bool an_vec_sort(an_vec_t *self, CmpFn cmp)
{
    if (is_null(self) || is_null(cmp))
    {
        return false;
    }

    qsort(self->allocation, self->len, self->ele_size, cmp);
    return true;
}

bool an_vec_is_sorted(an_vec_t *self, CmpFn cmp)
{
    size_t i;
    for (i = 0; i < self->len - 1; i++)
    {
        if (cmp(_an_vec_get(self, i), _an_vec_get(self, i + 1)) > 0)
        {
            return false;
        }
    }
    return true;
}

void *an_vec_index(const an_vec_t *self, const size_t index)
{
    if (is_null(self) || index >= self->len)
    {
        return NULL;
    }

    return _an_vec_get(self, index);
}

bool an_vec_pop(an_vec_t *self, void *dst)
{
    if (is_null(self) || self->len == 0 || is_null(dst))
    {
        return false;
    }

    memcpy(dst, _an_vec_get(self, --self->len), self->ele_size);
    return true;
}

size_t an_vec_len(const an_vec_t *self)
{
    if (is_null(self))
    {
        return 0;
    }

    return self->len;
}

size_t an_vec_capacity(const an_vec_t *self)
{
    if (is_null(self))
    {
        return 0;
    }

    return self->capacity;
}

size_t an_vec_element_size(const an_vec_t *self)
{
    if (is_null(self))
    {
        return 0;
    }

    return self->ele_size;
}

bool an_vec_append(an_vec_t *self, const void *arr, size_t append_n)
{
    return an_vec_splice(self, self->len, 0, arr, append_n);
}

bool an_vec_set(an_vec_t *self, const void *element, size_t index)
{
    if (is_null(self) || index > self->len)
    {
        return false;
    }

    memcpy(_an_vec_get(self, index), element, self->ele_size);
    return true;
}

bool an_vec_eq(const an_vec_t *self, const an_vec_t *other, CmpFn cmp_ele_fn)
{
    size_t len;

    if (is_null(self) || is_null(other) || (len = self->len) != other->len ||
        self->ele_size != other->ele_size)
    {
        return false;
    }

    if (is_some(cmp_ele_fn))
    {
        /* Compare element by element */
        for (size_t i = 0; i < len; i++)
        {
            if (cmp_ele_fn(_an_vec_get(self, i), _an_vec_get(other, i)) != 0)
            {
                return false;
            }
        }
    }
    else
    {
        /* Compare the whole allocation at once */
        if (memcmp(self->allocation, other->allocation, len * self->ele_size) != 0)
        {
            return false;
        }
    }

    return true;
}

bool an_vec_get(an_vec_t *self, size_t index, void *out)
{
    if (is_null(self) || index >= self->len || is_null(out))
    {
        return false;
    }

    memcpy(out, _an_vec_get(self, index), self->ele_size);
    return true;
}

bool an_vec_insert(an_vec_t *self, const void *element, const size_t index)
{
    if (is_null(self) | is_null(element) | (index > self->len) |
        (_an_vec_ensure(self, 1) == -1))
    {
        return false;
    }
    else if (index == self->len)
    {
        return an_vec_push(self, element);
    }
    else
    {
        byte *hole = _an_vec_get(self, index + 1);
        size_t elsize = self->ele_size;

        memmove(hole, hole - elsize, (self->len - index) * elsize);
        memcpy(_an_vec_get(self, index), element, elsize);
        self->len++;
        return true;
    }
}

bool an_vec_remove(an_vec_t *self, const size_t index, FreeFn free_fn)
{
    if (is_null(self) || index >= self->len)
    {
        return false;
    }

    byte *hole = _an_vec_get(self, index);

    if (free_fn)
    {
        free_fn(hole);
    }

    size_t elsize = self->ele_size;

    memmove(hole, hole + elsize, (--self->len - index) * elsize);
    return true;
}

static void an_vec_drop_elements(an_vec_t *self, FreeFn free_fn)
{
    for (size_t size = self->len; size > 0; size--)
    {
        free_fn(_an_vec_get(self, size - 1));
    }
}

void an_vec_drop(an_vec_t **vecp, FreeFn free_fn)
{
    an_vec_t *self;
    if (is_null(vecp) || is_null((self = *vecp)))
    {
        return;
    }

    if (is_some(self->allocation))
    {
        if (is_some(free_fn))
        {
            an_vec_drop_elements(self, free_fn);
        }
        free(self->allocation);
    }

    self->allocation = NULL;
    self->capacity = 0;
    self->ele_size = 0;
    self->len = 0;
    free(self);
    *vecp = NULL;
}
