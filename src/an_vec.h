#ifndef __AN_VEC_H__
#define __AN_VEC_H__

#include <stdbool.h>
#include <stdio.h>

typedef int (*CmpFn)(const void *, const void *);

typedef void (*FreeFn)(void *);

typedef struct _vec_t vec_t;

vec_t *vec_new(const size_t ele_size);

vec_t *vec_with_capacity(const size_t ele_size, const size_t capacity);

bool vec_push(vec_t *vec, const void *element);

bool vec_pop(vec_t *vec, void *dst);

size_t vec_len(const vec_t *vec);

size_t vec_max_size(void);

size_t vec_capacity(const vec_t *vec);

size_t vec_element_size(const vec_t *vec);

void vec_clear(vec_t *vec, FreeFn free_fn);

bool vec_sort(vec_t *vec, CmpFn cmp);

bool vec_is_sorted(vec_t *vec, CmpFn cmp);

void vec_drop(vec_t **vec, FreeFn free_fn);

void *vec_index(const vec_t *vec, const size_t index);

void *vec_front(const vec_t *vec);

void *vec_inner_unsafe(const vec_t *vec);

void *vec_back(const vec_t *vec);

bool vec_insert_non_contiguous(vec_t *vec, const void *element,
                                  const size_t index);

void *vec_index_unsafe(const vec_t *vec, const size_t index);

bool vec_insert(vec_t *vec, const void *element, const size_t index);

bool vec_splice(vec_t *self, size_t index, size_t remove_n,
                   const void *array, size_t insert_n);

bool vec_append(vec_t *self, const void *arr, size_t append_n);

bool vec_get(vec_t *self, size_t index, void *out);

bool vec_set(vec_t *self, const void *element, size_t index);

bool vec_remove(vec_t *vec, const size_t index, FreeFn free_fn);

bool vec_remove_at_ptr(vec_t *self, void *ptr, FreeFn free_fn);

bool vec_eq(const vec_t *self, const vec_t *other, CmpFn cmp_ele_fn);

#endif
