#ifndef __AN_CHITER_H__
#define __AN_CHITER_H__

#include "an_string.h"
#include "an_utils.h"
#include <stdbool.h>

typedef struct {
  byte *next;
  byte *end;
} chariter_t;

chariter_t chariter_new(byte *start, size_t len);

chariter_t chariter_from_string(string_t *string);

int chariter_next(chariter_t *iter);

int chariter_goback_unsafe(chariter_t *iter, size_t steps);

int chariter_peek(chariter_t *iter);

bool chariter_set_unsafe(chariter_t *iter, byte *next);

#endif
