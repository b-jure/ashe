#ifndef AUTILS_H
#define AUTILS_H

#include "token.h"
#include "acommon.h"

#include <stdarg.h>


typedef enum {
    INFO_NAME,
    INFO_DESC,
    INFO_USAGE,
} Info;


void die(void);
void die_err(const char* err);
void Shell_cleanup(void); // Definition in 'shell.c'

void vfprint_warning(const char* fmt, va_list argp);
void fprint_warning(const char* fmt, ...);
void fprint_error(const char* errfmt, ...);
void print_errno(void);
void print_info(Info type, const char* str);
void print_manpage(const char* name, const char* usage, const char* desc);

ubyte in_dq(char* str, memmax len);
ubyte is_escaped(char* bt, memmax curpos);

memmax len_without_seq(const char* prompt);

void unescape(Buffer* str);
void expand_vars(Buffer* str);

#endif
