#ifndef __ASH_TOKEN_H__
#define __ASH_TOKEN_H__

#include "ashe_string.h"
#include <stddef.h>

typedef enum {
  REDIROP_TOKEN = 1,
  WORD_TOKEN = 2,
  KVPAIR_TOKEN = 4,
  PIPE_TOKEN = 8,
  AND_TOKEN = 16,
  OR_TOKEN = 32,
  BG_TOKEN = 64,
  FG_TOKEN = 128,
  EOL_TOKEN = 256,
  OOM_TOKEN = 512,
} tokentype_t;

typedef struct {
  tokentype_t type;
  string_t *contents;
} token_t;

token_t token_new(tokentype_t ttype, const byte *str);

#endif
