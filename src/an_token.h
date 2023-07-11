#include "an_string.h"
#include <stddef.h>

typedef enum {
  REDIROP_TOKEN,
  WORD_TOKEN,
  PIPE_TOKEN,
  AND_TOKEN,
  OR_TOKEN,
  BG_TOKEN,
  FG_TOKEN,
  NAT_TOKEN,
  EOL_TOKEN
} an_tokentype_t;

typedef struct {
  an_tokentype_t type;
  an_string_t *contents;
} an_token_t;

an_token_t an_token_new(an_tokentype_t ttype, const byte *str);
