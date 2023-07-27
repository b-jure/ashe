#ifndef __ASH_STRING_H__
#define __ASH_STRING_H__

#include <stdbool.h>
#include <stddef.h>

typedef char byte;
typedef struct string_t string_t;

string_t *string_new(void);

string_t *string_with_cap(size_t capacity);

size_t string_len(string_t *self);

const char *string_ref(string_t *string);

byte *string_slice(string_t *string, size_t index);

string_t *string_from(const char *str);

int string_first(string_t *self);

int string_last(string_t *self);

bool string_splice(string_t *self, size_t index, size_t remove_n,
                   const char *str, size_t insert_n);

bool string_remove(string_t *self, size_t index);

bool string_remove_at_ptr(string_t *self, byte *ptr);

bool string_eq(string_t *self, string_t *other);

bool string_append(string_t *self, const void *str, size_t len);

bool string_set(string_t *self, const char ch, size_t index);

bool string_set_at_ptr(string_t *self, byte *ptr, char c);

int string_get(string_t *self, size_t index);

size_t string_len(string_t *self);

void string_drop(string_t *self);

void string_drop_inner(string_t *self);

bool string_clear(string_t *self);

#endif
