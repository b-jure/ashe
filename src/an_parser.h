#ifndef __AN_PARSER_H__
#define __AN_PARSER_H__

#include "an_utils.h"
#include <stddef.h>

typedef struct commandline_t commandline_t;

commandline_t commandline_new(void);
int parse_commandline(const byte *line, size_t len, commandline_t *out);

#endif
