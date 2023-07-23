#ifndef __AN_PARSER_H__
#define __AN_PARSER_H__

#include "cmdline.h"
#include <stddef.h>

int parse_commandline(const byte *line, commandline_t *out, bool *set_env);

typedef struct {
  vec_t *pipelines;   /* collection of pipelines */
  bool is_background; /* proccess is run in foreground otherwise background */
} conditional_t;

void conditional_drop(conditional_t *cond);

typedef struct {
  vec_t *argv; /* Command name and arguments */
  vec_t *env;  /* Program environmental variables */
} command_t;

typedef struct {
  vec_t *commands;
  int connection; /* Pipeline is connected with '&&' */
} pipeline_t;

#define ASH_AND (int)1
#define ASH_OR (int)2
#define ASH_NONE (int)4

#define IS_AND(connection) ((connection) == ASH_AND)
#define IS_OR(connection) ((connection) == ASH_OR)
#define IS_NONE(connection) ((connection) == ASH_NONE)

#endif
