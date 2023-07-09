#include "an_utils.h"
#include "an_vec.h"
#include <limits.h>
#include <memory.h>
#include <stdlib.h>

#define MAX_VEC_SIZE UINT_MAX

typedef char byte;

static void* _an_vec_get(const an_vec_t* vec, const size_t index);

struct _an_vec_t {
    byte*  allocation;
    size_t ele_size;
    size_t capacity;
    size_t len;
};

an_vec_t* an_vec_new(const size_t ele_size)
{
    an_vec_t* vec = malloc(sizeof(an_vec_t));

    if(is_null(vec)) { return NULL; }

    vec->ele_size   = ele_size;
    vec->capacity   = 0;
    vec->len        = 0;
    vec->allocation = NULL;

    return vec;
}

an_vec_t* an_vec_with_capacity(const size_t ele_size, const size_t capacity)
{
    an_vec_t* vec;
    void*     allocation;

    if(is_null(vec = an_vec_new(ele_size)) || is_null(allocation = calloc(capacity, ele_size)))
    {
        return NULL;
    }

    vec->allocation = allocation;
    vec->capacity   = capacity;

    return vec;
}

size_t an_vec_max_size(void)
{
    return MAX_VEC_SIZE;
}

an_vec_t* an_vec_splice(an_vec_t* self, size_t start, size_t end, size_t n)
{
    if(is_null(self) || self->len < end || start > end) { return NULL; }
    // TODO
    return NULL;
}

static int _an_vec_ensure(an_vec_t* vec, size_t bytes)
{
    size_t required = vec->len + bytes;

    if(required > vec->capacity)
    {

        if(required > MAX_VEC_SIZE)
        {
            fprintf(stderr, "%s:%d - allocation too big\n", __FILE__, __LINE__);
            return -1;
        } else if((required *= 2) > MAX_VEC_SIZE) {
            fprintf(stderr, "%s:%d - exceeded maxmimum 'an_vec' size\n", __FILE__, __LINE__);
            return -1;
        }

        void* new_alloc = realloc(vec->allocation, required);

        if(is_null(new_alloc))
        {
            fprintf(stderr, "%s:%d - out of memory\n", __FILE__, __LINE__);
            return -1;
        }

        vec->allocation = new_alloc;
        vec->capacity   = required;
    }

    return 0;
}

static void* _an_vec_get(const an_vec_t* vec, const size_t index)
{
    return (vec->allocation + (index * vec->ele_size));
}

void* an_vec_front(const an_vec_t* vec)
{
    if(is_null(vec) || vec->len == 0) { return NULL; }

    return _an_vec_get(vec, 0);
}

void* an_vec_inner_unsafe(const an_vec_t* vec)
{
    if(is_null(vec)) { return NULL; }
    return vec->allocation;
}

void* an_vec_back(const an_vec_t* vec)
{
    if(is_null(vec) || vec->len == 0) { return NULL; }

    return _an_vec_get(vec, vec->len - 1);
}

bool an_vec_push(an_vec_t* vec, const void* element)
{
    if(is_null(vec) || _an_vec_ensure(vec, 1) == -1) { return false; }

    memmove(_an_vec_get(vec, vec->len), element, vec->ele_size);
    vec->len++;

    return true;
}

void an_vec_clear(an_vec_t* vec, FreeFn free_fn)
{
    size_t len;

    if(is_null(vec) || is_null(vec->allocation)) { return; }

    if(free_fn)
    {
        for(len = vec->len; len--;)
        {
            free_fn(_an_vec_get(vec, len));
        }
    }

    free(vec->allocation);
    vec->allocation = NULL;
    vec->capacity   = 0;
    vec->len        = 0;
}

bool an_vec_sort(an_vec_t* vec, CmpFn cmp)
{
    if(is_null(vec) || is_null(cmp)) { return false; }

    qsort(vec->allocation, vec->len, vec->ele_size, cmp);
    return true;
}

bool an_vec_is_sorted(an_vec_t* vec, CmpFn cmp)
{
    size_t i;
    for(i = 0; i < vec->len - 1; i++)
    {
        if(cmp(_an_vec_get(vec, i), _an_vec_get(vec, i + 1)) > 0) { return false; }
    }
    return true;
}

void* an_vec_index(const an_vec_t* vec, const size_t index)
{
    if(is_null(vec) || index >= vec->len) { return NULL; }

    return _an_vec_get(vec, index);
}

bool an_vec_pop(an_vec_t* vec, void* dst)
{
    if(is_null(vec) || vec->len == 0 || is_null(dst)) { return false; }

    memcpy(dst, _an_vec_get(vec, --vec->len), vec->ele_size);
    return true;
}

size_t an_vec_len(const an_vec_t* vec)
{
    if(is_null(vec)) { return 0; }

    return vec->len;
}

size_t an_vec_capacity(const an_vec_t* vec)
{
    if(is_null(vec)) { return 0; }

    return vec->capacity;
}

size_t an_vec_element_size(const an_vec_t* vec)
{
    if(is_null(vec)) { return 0; }

    return vec->ele_size;
}

bool an_vec_insert(an_vec_t* vec, const void* element, const size_t index)
{
    if(is_null(vec) | is_null(element) | (index > vec->len) | (_an_vec_ensure(vec, 1) == -1))
    {
        return false;
    } else if(index == vec->len) {
        return an_vec_push(vec, element);
    } else {
        byte*  hole   = _an_vec_get(vec, index + 1);
        size_t elsize = vec->ele_size;

        memmove(hole, hole - elsize, (vec->len - index) * elsize);
        memcpy(_an_vec_get(vec, index), element, elsize);
        vec->len++;
        return true;
    }
}

bool an_vec_remove(an_vec_t* vec, const size_t index, FreeFn free_fn)
{
    if(is_null(vec) || index >= vec->len) { return false; }

    byte* hole = _an_vec_get(vec, index);

    if(free_fn) { free_fn(hole); }

    size_t elsize = vec->ele_size;

    memmove(hole, hole + elsize, (--vec->len - index) * elsize);
    return true;
}

static void an_vec_drop_elements(an_vec_t* vec, FreeFn free_fn)
{
    for(size_t size = vec->len; size > 0; size--)
    {
        free_fn(_an_vec_get(vec, size - 1));
    }
}

void an_vec_drop(an_vec_t* vec, FreeFn free_fn)
{
    if(is_null(vec)) { return; }

    if(is_some(vec->allocation))
    {
        if(is_some(free_fn)) { an_vec_drop_elements(vec, free_fn); }
        free(vec->allocation);
    }

    vec->allocation = NULL;
    vec->capacity   = 0;
    vec->ele_size   = 0;
    vec->len        = 0;
    free(vec);
}
