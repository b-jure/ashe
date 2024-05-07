// clang-format off
#ifndef ADBG_H
#define ADBG_H

#include <stdio.h>

#include "acommon.h"
#include "aparser.h"

#define ASHE_TAB 4

/* debug terminal input */
#define ALOG_CURSOR 0
#define ALOG_LINES  1
extern const char *logfiles[];
void debug_cursor(void);
void debug_lines(void);
void logfile_create(const char *logfile, a_int32 which);
void remove_logfiles(void);

/* debug lexer */
void debug_current_token(struct a_token *token);
void debug_token(struct a_token *token, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_toktype(enum a_toktype type, const char *name, a_uint32 tabs, a_arr_char *out);
const char *a_token_str(struct a_token *token);

/* debug parser (AST) */
void debug_ast(struct a_block *block);
/* structs */
void debug_block(struct a_block *block, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_list(struct a_list *list, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_pipeline(struct a_pipeline *pipeline, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_cmd(struct a_cmd *cmd, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_simple_cmd(struct a_simple_cmd *scmd, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_redirect(struct a_redirect *rd, const char *name, a_uint32 tabs, a_arr_char *out);
/* enums */
void debug_redirect_op(enum a_redirect_op op, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_connect(enum a_connect con, const char *name, a_uint32 tabs, a_arr_char *out);
/* arrays */
void debug_arr_list(a_arr_list *lists, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_arr_pipeline(a_arr_pipeline *pipes, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_arr_cmd(a_arr_cmd *cmds, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_arr_redirect(a_arr_redirect *rds, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_arr_ccharp(a_arr_ccharp *arr, const char *name, a_uint32 tabs, a_arr_char *out);
/* terms */
void debug_boolean(a_ubyte b, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_ccharp(const char *ptr, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_ptr(const void *ptr, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_string(const char *str, a_memmax len, const char *name, a_uint32 tabs, a_arr_char *out);
void debug_number(a_ssize n, const char *name, a_uint32 tabs, a_arr_char *out);

#endif
// clang-format on
