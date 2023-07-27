#ifndef __ASH_CHITER_H__
#define __ASH_CHITER_H__

#include "ashe_string.h"

#ifndef EOL
#define EOL '\0'
#endif

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
