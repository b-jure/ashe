#include "an_string.h"
#include <stddef.h>

typedef enum {
  PIPE_TOKEN,
  REDIR_O_CRT_TOKEN,
  REDIR_O_APN_TOKEN,
  REDIR_I_TOKEN,
  ID_TOKEN,
  EOL_TOKEN,
  NAT_TOKEN
} an_tokentype_t;

typedef struct {
  an_tokentype_t type;
  an_string_t *contents;
} an_token_t;

an_token_t an_token_new(an_tokentype_t ttype, const byte *str);
