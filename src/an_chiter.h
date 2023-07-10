#ifndef __AN_CHITER_H__
#define __AN_CHITER_H__

#include "an_string.h"
#include "an_utils.h"

typedef struct {
  byte *next;
  byte *end;
} an_chariter_t;

an_chariter_t an_chariter_new(byte *start, size_t len);

an_chariter_t an_chariter_from_string(an_string_t *string);

int an_chariter_next(an_chariter_t *iter);

int an_chariter_peek(an_chariter_t *iter);

#endif
