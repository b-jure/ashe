#include "acommon.h"
#include "ashe_utils.h"
#include "lexer.h"
#include "shell.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>


/* error token buffer size */
#define BUFF_ERR_SIZE 100


/* integers returned from 'fd_left()' and 'fd_right()'. */
#define BIGFD(fd) ((fd) == -1)
#define BADFD(fd) ((fd) == -2)


/*
 * TODO: Implement ****GLOB OPERATOR****, and rest of the regular expressions
 * #include <glob.h>
 */



/* Lexer errors */
#define ERR_FDOVF 0
#define ERR_FDINV 1
static const char* lexerrors[] = {
    "file descriptor too large '%s'.",
    "invalid file descriptor.",
};




/* Return 1 if character 'c' is a single byte (char) token. */
static finline ubyte is1btoken(int32 c)
{
    switch(c) {
        case '&':
        case '|':
        case '>':
        case '<':
        case ';':
        case '[':
        case ']':
        case '?':
        case '*':
        case '!':
        case '{':
        case '}': return 1;
        default: return 0;
    }
}



static finline void Token_init(Token* token)
{
    memset(token, 1, sizeof(Token));
}

static finline void Token_free(Token* token)
{
    Buffer_free(&TB(token), NULL);
}



void Lexer_init(Lexer* lexer, const char* start)
{
    lexer->current = lexer->start = start;
}

void Lexer_free(Lexer* lexer)
{
    Token_free(&lexer->prev);
    Token_free(&lexer->curr);
}

/* Peek 'amount' without advancing. */
static finline int32 peek(Lexer* lexer, memmax amount)
{
    return *(lexer->current + amount);
}

/* Advance buffer by a single character unless EOF is reached. */
static finline int32 advance(Lexer* lexer)
{
    int32 c = *lexer->current;
    if(c != '\0') lexer->current++;
    return c;
}

/* Gets a string, expands environmental variables and unescapes it. */
static Token Lexer_string(Lexer* lexer)
{
    Token token;
    ubyte dq, escape;
    Token_init(&token);
    dq = escape = 0;
    token.type = TOKEN_WORD;
    token.start = lexer->current;
    Buffer* buffer = &TB(&token);
    int32 c;
    while(((c = advance(lexer)) != '\0')) {
        if(!dq && !escape && ((isspace(c)) || is1btoken(c))) break;
        if(!escape && c == '"') dq ^= 1;
        else if(c == '\\' || escape) escape ^= 1;
        Buffer_push(buffer, c);
    }
    Buffer_push(buffer, '\0');
    token.len = lexer->current - token.start;
    expand_vars(buffer);
    char* ptr = buffer->data;
    if(*ptr != '=' && (ptr = strstr(ptr, "=")) != NULL) {
        *ptr = '\0';
        memmax klen = strlen(buffer->data);
        if(strspn(buffer->data, ENV_VAR_CHARS) == klen)
            token.type = TOKEN_KVPAIR;
        *ptr = '=';
    }
    unescape(buffer);
    ArrayCharptr_push(&ashe.sh_buffers, TS(&token));
    return token;
}

/* Skip whitespace characters */
static void lexer_skip_ws(Lexer* lexer)
{
    int32 c = peek(lexer, 0);
    for(;;) {
        switch(c) {
            case '\n':
            case '\r':
            case ' ':
            case '\t':
            case '\v': c = advance(lexer);
            default: return;
        }
    }
}

/* Auxiliary to 'Token_tostr()' */
static finline const char* tokfstr(memmax len, const char* str)
{
    static char buffer[UINT_DIGITS];
    snprintf(buffer, UINT_DIGITS, "%.*s", (int32)len, str);
    return buffer;
}

/* Warning: each time you invoke this function
 * the previous string might become invalid.
 * This is because some tokens use static buffer
 * as storage to avoid allocating, so make sure each
 * time you invoke this that you don't need the result
 * of the previous call to this function. */
const char* Token_tostr(Token* token)
{
    switch(token->type)
    {
        case TOKEN_WORD:
        case TOKEN_KVPAIR:
        case TOKEN_ERROR: 
            return TS(token);
        case TOKEN_PIPE: return "|";
        case TOKEN_AND: return "&&";
        case TOKEN_OR: return "||";
        case TOKEN_BG: return "&";
        case TOKEN_SEPARATOR:
        case TOKEN_FDREAD:
        case TOKEN_FDWRITE:
        case TOKEN_FDREDIRECT:
        case TOKEN_FDCLOSE: 
            return tokfstr(token->len, token->start);
        case TOKEN_EOF: return NULL;
    }
}

/* Get positive integer for file descriptor.
 * Return -1 in case of overflow. */
static finline int32 posint(const char** ptr)
{
    int32 c;
    uint32 fd, digits;
    const char* str = *ptr;
    digits = fd = 0;
    while(isdigit((c = *str++))) {
        fd = fd * 10 + (c - '0');
        digits++;
        if(digits > 10) break;
    }
    *ptr = str;
    if(digits > 10 || (digits == 10 && fd > INT32_MAX)) return -1;
    return fd;
}

/* File descriptor on the left side (5): 'exec 5<file.txt', 'cat 5<&8', ...
 * Return -1 if overflow occurred or would occur (fd is too large for int32).
 * Return -2 if the file descriptor is invalid.
 * Return non-negative integer (fd) in case of no errors. */
static int32 fd_left(const char* str)
{
    int32 fd, c;
    fd = posint(&str);
    c = *str;
    if(!isspace(c) && !is1btoken(c)) return -2;
    return fd;
}

/* File descriptor on the right side (1): 'exec 10>&1'
 * Return -1 if overflow occurred or would occur (fd is too large for int32).
 * Return -2 if the file descriptor is invalid.
 * Return non-negative integer (fd) in case of no errors. */
static int32 fd_right(const char* str)
{
    int32 fd, c;
    fd = posint(&str);
    c = *str;
    if(!isspace(c)) return -2;
    else return fd;
}

static Token Token_error(const char* errfmt, ...)
{
    static char buffer[BUFF_ERR_SIZE];
    Token token;
    Token_init(&token);
    va_list argp;
    va_start(argp, errfmt);
    int32 n = vsnprintf(buffer, BUFF_ERR_SIZE, errfmt, argp);
    va_end(argp);
    if(unlikely(n < 0)) die();
    return token;
}


/* Code crimes submission function. */
Token Lexer_next(Lexer* lexer)
{
    int32 c, fd;
    Tokentype ttype;
    Token token; // for fd string
    Token_init(&token);
    Token_init(&lexer->curr);
    lexer_skip_ws(lexer);
    switch((c = peek(lexer, 0))) {
        case '<': {
            advance(lexer);
            TF(&lexer->curr).fd_left = 0;
            lexer->curr.type = TOKEN_FDREAD;
            goto l_check_right_fd;
        }
        case '>': {
            advance(lexer);
            TF(&lexer->curr).fd_left = 1;
            lexer->curr.type = TOKEN_FDWRITE;
        l_fd_write:
            if(peek(lexer, 0) == '>') { // append ?
                advance(lexer);
                lexer->curr.u.file.append = 1;
            } else {
                lexer->curr.u.file.append = 0;
            l_check_right_fd:
                if(peek(lexer, 0) == '&') {
                    advance(lexer);
                    c = peek(lexer, 0);
                    if(c == '-') { // close file descriptor ?
                        advance(lexer);
                        if(!isspace(peek(lexer, 0))) goto l_err_fd_invalid;
                        lexer->curr.type = TOKEN_FDCLOSE;
                    } else if(isdigit(c)) {
                        token = Lexer_string(lexer);
                        if(!isspace(peek(lexer, 0))) goto l_err_fd_invalid;
                        fd = fd_right(TS(&token));
                        if(BIGFD(fd)) goto l_err_fd_overflow;
                        if(BADFD(fd)) goto l_err_fd_invalid;
                        TF(&lexer->curr).fd_right = fd;
                        return lexer->curr;
                    } else goto l_err_fd_invalid;
                }
            }
            return lexer->curr;
        }
        case '&': {
            if((c = peek(lexer, 1)) == '&') {
                advance(lexer);
                ttype = TOKEN_AND;
            } else ttype = TOKEN_BG;
            break;
        }
        case '|': {
            if(peek(lexer, 1) == '|') {
                advance(lexer);
                ttype = TOKEN_OR;
            } else ttype = TOKEN_PIPE;
            break;
        }
        case ';': ttype = TOKEN_SEPARATOR; break;
        case '\0': ttype = TOKEN_EOF; break;
        default: {
            lexer->curr = Lexer_string(lexer);
            fd = fd_left(TS(&token));
            if(BIGFD(fd)) goto l_err_fd_overflow;
            if(!BADFD(fd)) {
                c = peek(lexer, 0);
                if(c == '>' || c == '<') {
                    Buffer_free(&TB(&lexer->curr), NULL); // don't need the string
                    advance(lexer);
                    lexer->curr.type = (c == '>' ? TOKEN_FDWRITE : TOKEN_FDREAD);
                    TF(&lexer->curr).write = lexer->curr.type & TOKEN_FDWRITE;
                    TF(&lexer->curr).fd_left = fd;
                    if(lexer->curr.type & TOKEN_FDWRITE) goto l_fd_write; // check for append '>>' + right side fd
                    else goto l_check_right_fd; // check for right side fd
                }
            }
            return lexer->curr; // string
        }
    }
    advance(lexer);
    lexer->curr.type = ttype;
    return lexer->curr;
l_err_fd_overflow:
    lexer->curr = Token_error(lexerrors[ERR_FDOVF], TS(&token));
    goto l_defer;
l_err_fd_invalid:
    lexer->curr = Token_error(lexerrors[ERR_FDINV]);
l_defer:
    Buffer_free(&TB(&token), NULL);
    Buffer_free(&TB(&lexer->curr), NULL);
    return lexer->curr;
}
