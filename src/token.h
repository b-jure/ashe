#ifndef ATOKEN_H
#define ATOKEN_H

#include "aarray.h"
#include "acommon.h"

ARRAY_NEW(Buffer, char);

typedef enum {
    TOKEN_WORD = (1 << 0),
    TOKEN_KVPAIR = (1 << 1),
    TOKEN_PIPE = (1 << 2),
    TOKEN_AND = (1 << 3),
    TOKEN_OR = (1 << 4),
    TOKEN_BG = (1 << 5),
    TOKEN_SEPARATOR = (1 << 6),
    TOKEN_EOF = (1 << 7),
    TOKEN_FDWRITE = (1 << 8),
    TOKEN_FDREAD = (1 << 9),
    TOKEN_FDREDIRECT = (1 << 10),
    TOKEN_FDCLOSE = (1 << 11),
    TOKEN_ERROR = (1 << 12),
} Tokentype;

typedef struct {
    Tokentype type;
    union {
        Buffer contents;
        struct {
            int32 fd_left;
            int32 fd_right;
            ubyte append : 1;
            ubyte write : 1;
        } file;
    } u;
    const char* start; // debug
    memmax len;        // debug
} Token;

/* token file */
#define TF(token) ((token)->u.file)
/* token buffer */
#define TB(token) ((token)->u.contents)
/* token string */
#define TS(token) ((token)->u.contents.data)



#endif
