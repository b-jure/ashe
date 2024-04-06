// clang-format off
#ifndef ADBG_H
#define ADBG_H

#include <stdio.h>

#include "acommon.h"
#include "aparser.h"

/* debug terminal input */
#define ALOG_CURSOR 0
#define ALOG_LINES  1
extern const char *logfiles[];
void debug_cursor(void);
void debug_lines(void);
void logfile_create(const char *logfile, int32 which);
void remove_logfiles(void);

/* debug lexer */
void debug_token(struct a_token *token);
const char *a_token_str(struct a_token *token);

/* debug parser (AST) */
/* structs */
void debug_block(struct a_block *block, const char *name, uint32 tabs, a_arr_char *out);
void debug_list(struct a_list *list, const char *name, uint32 tabs, a_arr_char *out);
void debug_pipeline(struct a_pipeline *pipeline, const char *name, uint32 tabs, a_arr_char *out);
void debug_cmd(struct a_cmd *cmd, const char *name, uint32 tabs, a_arr_char *out);
void debug_simple_cmd(struct a_simple_cmd *scmd, const char *name, uint32 tabs, a_arr_char *out);
void debug_redirect(struct a_redirect *rd, const char *name, uint32 tabs, a_arr_char *out);
/* enums */
void debug_redirect_op(enum a_redirect_op op, const char *name, uint32 tabs, a_arr_char *out);
void debug_connect(enum a_connect con, const char *name, uint32 tabs, a_arr_char *out);
/* arrays */
void debug_arr_list(a_arr_list *lists, const char *name, uint32 tabs, a_arr_char *out);
void debug_arr_pipeline(a_arr_pipeline *pipes, const char *name, uint32 tabs, a_arr_char *out);
void debug_arr_cmd(a_arr_cmd *cmds, const char *name, uint32 tabs, a_arr_char *out);
void debug_arr_redirect(a_arr_redirect *rds, const char *name, uint32 tabs, a_arr_char *out);
void debug_arr_ccharp(a_arr_ccharp *arr, const char *name, uint32 tabs, a_arr_char *out);
/* terms */
void debug_boolean(ubyte b, const char *name, uint32 tabs, a_arr_char *out);
void debug_ccharp(const char *ptr, const char *name, uint32 tabs, a_arr_char *out);
void debug_number(ssize n, const char *name, uint32 tabs, a_arr_char *out);

#endif
// clang-format on
