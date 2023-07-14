#ifndef __ASH_TOKEN_H__
#define __ASH_TOKEN_H__

#include "string.h"
#include <stddef.h>

typedef enum {
  REDIROP_TOKEN = 0,
  WORD_TOKEN = 1,
  KVPAIR_TOKEN = 2,
  PIPE_TOKEN = 4,
  AND_TOKEN = 8,
  OR_TOKEN = 16,
  BG_TOKEN = 32,
  FG_TOKEN = 64,
  NAT_TOKEN = 128,
  EOL_TOKEN = 256,
} tokentype_t;

typedef struct {
  tokentype_t type;
  string_t *contents;
} token_t;

token_t token_new(tokentype_t ttype, const byte *str);

#endif
