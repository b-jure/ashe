#ifndef __AN_VEC_H__
#define __AN_VEC_H__

#include <stdbool.h>
#include <stdio.h>

typedef int (*CmpFn)(const void *, const void *);

typedef void (*FreeFn)(void *);

typedef struct _an_vec_t an_vec_t;

an_vec_t *an_vec_new(const size_t ele_size);

an_vec_t *an_vec_with_capacity(const size_t ele_size, const size_t capacity);

bool an_vec_push(an_vec_t *vec, const void *element);

bool an_vec_pop(an_vec_t *vec, void *dst);

size_t an_vec_len(const an_vec_t *vec);

size_t an_vec_max_size(void);

size_t an_vec_capacity(const an_vec_t *vec);

size_t an_vec_element_size(const an_vec_t *vec);

void an_vec_clear(an_vec_t *vec, FreeFn free_fn);

bool an_vec_sort(an_vec_t *vec, CmpFn cmp);

bool an_vec_is_sorted(an_vec_t *vec, CmpFn cmp);

void an_vec_drop(an_vec_t *vec, FreeFn free_fn);

void *an_vec_index(const an_vec_t *vec, const size_t index);

void *an_vec_front(const an_vec_t *vec);

void *an_vec_inner_unsafe(const an_vec_t *vec);

void *an_vec_back(const an_vec_t *vec);

bool an_vec_insert_non_contiguous(an_vec_t *vec, const void *element,
                                  const size_t index);

void *an_vec_index_unsafe(const an_vec_t *vec, const size_t index);

bool an_vec_insert(an_vec_t *vec, const void *element, const size_t index);

bool an_vec_remove(an_vec_t *vec, const size_t index, FreeFn free_fn);

#endif
