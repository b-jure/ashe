#ifndef GTEST
    #include "async.h"
    #include "errors.h"
#endif
#include "vec.h"

#include <limits.h>
#include <memory.h>
#include <stdlib.h>

#if !defined(is_null) || !defined(is_some)
    #define is_null(ptr) ((ptr) == NULL)
    #define is_some(ptr) ((ptr) != NULL)
#endif

#define MAX_VEC_SIZE UINT_MAX
#define ptr_in_bounds(vec, ptr)                                   \
    (is_some((vec)) && is_some((vec)->allocation) && is_some(ptr) \
     && (vec)->allocation + ((vec)->ele_size * (vec)->len) > (ptr) && (ptr) >= (vec)->allocation)

static void *_an_vec_get(const vec_t *self, const size_t index);
static int _an_vec_ensure(vec_t *self, size_t bytes);

struct _vec_t {
    byte *allocation;
    size_t ele_size;
    size_t capacity;
    size_t len;
};

vec_t *vec_new(const size_t ele_size)
{
    vec_t *vec = malloc(sizeof(vec_t));

    if(__glibc_unlikely(is_null(vec))) {
#ifndef GTEST
        ATOMIC_PRINT({
            PW_OOM(sizeof(vec_t));
            perr();
        });
#endif
        return NULL;
    }

    vec->ele_size = ele_size;
    vec->capacity = 0;
    vec->len = 0;
    vec->allocation = NULL;

    return vec;
}

vec_t *vec_with_capacity(const size_t ele_size, const size_t capacity)
{
    vec_t *vec;
    void *allocation;

    if(__glibc_unlikely(is_null(vec = vec_new(ele_size)))) {
        return NULL;
    } else if(__glibc_unlikely(is_null(allocation = calloc(capacity, ele_size)))) {
#ifndef GTEST
        ATOMIC_PRINT({
            PW_OOM(capacity * ele_size);
            perr();
        });
#endif
        return NULL;
    }

    vec->allocation = allocation;
    vec->capacity = capacity;

    return vec;
}

size_t vec_max_size(void)
{
    return MAX_VEC_SIZE;
}

bool vec_splice(vec_t *self, size_t index, size_t remove_n, const void *array, size_t insert_n)
{
    if(is_null(self) || is_null(array) || self->len <= index + remove_n
       || __glibc_unlikely((_an_vec_ensure(self, insert_n - remove_n) == -1)))
    {
        return false;
    }

    memcpy(
        _an_vec_get(self, index + insert_n),
        _an_vec_get(self, index + remove_n),
        self->ele_size * (self->len - (index + remove_n)));
    memcpy(_an_vec_get(self, index), array, self->ele_size * insert_n);

    self->len += (insert_n - remove_n);

    return true;
}

static int _an_vec_ensure(vec_t *self, size_t bytes)
{
    size_t required = self->len + bytes;
    void *new_alloc = NULL;

    if(is_null(self->allocation)) {
        new_alloc = calloc(required, self->ele_size);
    } else if(required > self->capacity) {
        if(__glibc_unlikely(required > MAX_VEC_SIZE)) {
            return -1;
        } else if(__glibc_unlikely(required > (MAX_VEC_SIZE / 2))) {
            required = MAX_VEC_SIZE;
        } else {
            required *= 2;
        }

        new_alloc = realloc(self->allocation, required * self->ele_size);
    } else
        return 0;

    if(__glibc_unlikely(is_null(new_alloc))) {
        return -1;
    }

    self->allocation = new_alloc;
    self->capacity = required;

    return 0;
}

static void *_an_vec_get(const vec_t *self, const size_t index)
{
    return (self->allocation + (index * self->ele_size));
}

void *vec_front(const vec_t *self)
{
    if(is_null(self) || self->len == 0) {
        return NULL;
    }

    return _an_vec_get(self, 0);
}

void *vec_inner_unsafe(const vec_t *self)
{
    if(is_null(self)) {
        return NULL;
    }
    return self->allocation;
}

bool vec_remove_at_ptr(vec_t *self, void *ptr, FreeFn free_fn)
{
    if(!ptr_in_bounds(self, (byte *) ptr)) {
        return false;
    }

    size_t index = ((byte *) ptr - self->allocation) / self->ele_size;
    return vec_remove(self, index, free_fn);
}

bool vec_set_at_ptr(vec_t *self, void *ptr, void *element)
{
    if(!ptr_in_bounds(self, (byte *) ptr)) {
        return false;
    }

    size_t index = ((byte *) ptr - self->allocation) / self->ele_size;
    return vec_set(self, element, index);
}

void *vec_back(const vec_t *self)
{
    if(is_null(self) || self->len == 0) {
        return NULL;
    }

    return _an_vec_get(self, self->len - 1);
}

bool vec_push(vec_t *self, const void *element)
{
    if(is_null(self) || __glibc_unlikely(_an_vec_ensure(self, 1) == -1)) {
        return false;
    }

    memmove(_an_vec_get(self, self->len), element, self->ele_size);
    self->len++;

    return true;
}

void vec_clear(vec_t *self, FreeFn free_fn)
{
    size_t len;

    if(is_null(self) || self->len == 0) {
        return;
    }

    if(free_fn) {
        for(len = self->len; len--;) {
            free_fn(_an_vec_get(self, len));
        }
    }

    self->len = 0;
}

void vec_clear_capacity(vec_t *self, FreeFn free_fn)
{
    size_t len;

    if(is_null(self) || self->len == 0) {
        return;
    }

    if(free_fn) {
        for(len = self->len; len--;) {
            free_fn(_an_vec_get(self, len));
        }
    }

    self->len = 0;
    free(self->allocation);
    self->allocation = NULL;
    self->capacity = 0;
}

bool vec_sort(vec_t *self, CmpFn cmp)
{
    if(is_null(self) || is_null(cmp)) {
        return false;
    }

    qsort(self->allocation, self->len, self->ele_size, cmp);
    return true;
}

bool vec_is_sorted(vec_t *self, CmpFn cmp)
{
    size_t i;
    for(i = 0; i < self->len - 1; i++) {
        if(cmp(_an_vec_get(self, i), _an_vec_get(self, i + 1)) > 0) {
            return false;
        }
    }
    return true;
}

void *vec_index(const vec_t *self, const size_t index)
{
    if(is_null(self) || index >= self->len) {
        return NULL;
    }

    return _an_vec_get(self, index);
}

bool vec_pop(vec_t *self, void *dst)
{
    if(is_null(self) || self->len == 0 || is_null(dst)) {
        return false;
    }

    memcpy(dst, _an_vec_get(self, --self->len), self->ele_size);
    return true;
}

bool vec_pop_at(vec_t *self, void *dst, size_t index)
{
    if(is_null(self) || self->len == 0 || is_null(dst) || index >= self->len) {
        return false;
    }

    memcpy(dst, _an_vec_get(self, index), self->ele_size);
    self->len--;
    return true;
}

size_t vec_len(const vec_t *self)
{
    if(is_null(self)) {
        return 0;
    }

    return self->len;
}

size_t vec_capacity(const vec_t *self)
{
    if(is_null(self)) {
        return 0;
    }

    return self->capacity;
}

size_t vec_element_size(const vec_t *self)
{
    if(is_null(self)) {
        return 0;
    }

    return self->ele_size;
}

bool vec_append(vec_t *self, const void *arr, size_t append_n)
{
    return vec_splice(self, self->len, 0, arr, append_n);
}

bool vec_set(vec_t *self, const void *element, size_t index)
{
    if(is_null(self) || index > self->len) {
        return false;
    }

    memcpy(_an_vec_get(self, index), element, self->ele_size);
    return true;
}

bool vec_eq(const vec_t *self, const vec_t *other, CmpFn cmp_ele_fn)
{
    size_t len;

    if(is_null(self) || is_null(other) || (len = self->len) != other->len
       || self->ele_size != other->ele_size)
    {
        return false;
    }

    if(is_some(cmp_ele_fn)) {
        /* Compare element by element */
        for(size_t i = 0; i < len; i++) {
            if(cmp_ele_fn(_an_vec_get(self, i), _an_vec_get(other, i)) != 0) {
                return false;
            }
        }
    } else {
        /* Compare the whole allocation at once */
        if(memcmp(self->allocation, other->allocation, len * self->ele_size) != 0) {
            return false;
        }
    }

    return true;
}

bool vec_get(vec_t *self, size_t index, void *out)
{
    if(is_null(self) || index >= self->len || is_null(out)) {
        return false;
    }

    memcpy(out, _an_vec_get(self, index), self->ele_size);
    return true;
}

bool vec_insert(vec_t *self, const void *element, const size_t index)
{
    if(is_null(self) | is_null(element) | (index > self->len)
       | __glibc_unlikely((_an_vec_ensure(self, 1) == -1)))
    {
        return false;
    } else if(index == self->len) {
        return vec_push(self, element);
    } else {
        byte *hole = _an_vec_get(self, index + 1);
        size_t elsize = self->ele_size;

        memmove(hole, hole - elsize, (self->len - index) * elsize);
        memcpy(_an_vec_get(self, index), element, elsize);
        self->len++;
        return true;
    }
}

bool vec_remove(vec_t *self, const size_t index, FreeFn free_fn)
{
    if(is_null(self) || index >= self->len) {
        return false;
    }

    byte *hole = _an_vec_get(self, index);

    if(free_fn) {
        free_fn(hole);
    }

    size_t elsize = self->ele_size;

    memmove(hole, hole + elsize, (--self->len - index) * elsize);
    return true;
}

static void vec_drop_elements(vec_t *self, FreeFn free_fn)
{
    for(size_t size = self->len; size > 0; size--) {
        free_fn(_an_vec_get(self, size - 1));
    }
}

void vec_drop(vec_t **vecp, FreeFn free_fn)
{
    vec_t *self;
    if(is_null(vecp) || is_null((self = *vecp))) {
        return;
    }

    if(is_some(self->allocation)) {
        if(is_some(free_fn)) {
            vec_drop_elements(self, free_fn);
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
