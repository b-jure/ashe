#ifndef __AN_STRING_H__
#define __AN_STRING_H__

#include "an_utils.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct an_string_t an_string_t;

an_string_t *an_string_new(void);

an_string_t *an_string_with_cap(size_t capacity);

size_t an_string_len(an_string_t *self);

const char *an_string_ref(an_string_t *string);

byte *an_string_slice(an_string_t *string, size_t index);

an_string_t *an_string_from(const char *str);

bool an_string_splice(an_string_t *self, size_t index, size_t remove_n,
                      const char *str, size_t insert_n);

bool an_string_eq(an_string_t *self, an_string_t *other);

bool an_string_append(an_string_t *self, const void *str, size_t len);

bool an_string_set(an_string_t *self, const char ch, size_t index);

int an_string_get(an_string_t *self, size_t index);

size_t an_string_len(an_string_t *self);

void an_string_drop(an_string_t **self);

#endif
