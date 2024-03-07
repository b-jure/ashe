#include "acommon.h"
#include "ashe_utils.h"
#include "lexer.h"
#include "shell.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>


/* error token buffer size */
#define BUFF_ERR_SIZE 100


/* integers returned from fd_left() and fd_right(). */
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
    "invalid file descriptor '%s'.",
};


static finline ubyte isres(int32 c)
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

void Lexer_init(Lexer* lexer, const char* start)
{
    lexer->current = lexer->start = start;
}

static finline int32 peek(Lexer* lexer, memmax amount)
{
    return *(lexer->current + amount);
}

static finline int32 advance(Lexer* lexer)
{
    int32 c = *lexer->current;
    if(c != '\0') lexer->current++;
    return c;
}

static finline void Token_init(Token* token)
{
    Buffer_init(&token->u.contents);
    TF(token).fd_left = -1;
    TF(token).fd_right = -1;
    TF(token).read = 0;
    TF(token).append = 0;
}


static Token Lexer_string(Lexer* lexer)
{
    Token token;
    ubyte dq, escape;
    Token_init(&token);
    dq = escape = 0;
    token.type = TOKEN_WORD;
    Buffer* buffer = &token.u.contents;
    int32 c;
    while(((c = advance(lexer)) != '\0')) {
        if(!dq && !escape && ((isspace(c)) || isres(c))) break;
        if(!escape && c == '"') dq ^= 1;
        else if(c == '\\' || escape) escape ^= 1;
        Buffer_push(buffer, c);
    }
    Buffer_push(buffer, '\0');
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
    ArrayCharptr_push(&ashe.sh_buffers, token.u.contents.data);
    return token;
}

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

static finline const char* tokfstr(const char* fmt, ...)
{
    static char buffer[UINT_DIGITS];
    va_list argp;
    va_start(argp, fmt);
    vsnprintf(buffer, UINT_DIGITS, fmt, argp);
    va_end(argp);
    return buffer;
}

const char* Token_tostr(Token* token)
{
    switch(token->type)
    {
        case TOKEN_WORD:
        case TOKEN_KVPAIR:
        case TOKEN_ERROR: return TS(token);
        case TOKEN_PIPE: return "|";
        case TOKEN_AND: return "&&";
        case TOKEN_OR: return "||";
        case TOKEN_BG: return "&";
        case TOKEN_SEPARATOR: return ";";
        case TOKEN_FDREAD: return tokfstr("%d<", TF(token).fd_left);
        case TOKEN_FDWRITE: return tokfstr("%d>", TF(token).fd_left);
        case TOKEN_FDREDIRECT: return tokfstr("%d%c&%d", TF(token).fd_left, (TF(token).read ? '<' : '>'), TF(token).fd_right);
        case TOKEN_FDCLOSE: return tokfstr("%d", TF(token).fd_left);
        case TOKEN_EOF: return NULL;
    }
}

/* Lex positive integer. */
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
    if(!isspace(c) && !isres(c)) return -2;
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
    Buffer_push_str(&token.u.contents, buffer, n);
    ArrayCharptr_push(&ashe.sh_buffers, token.u.contents.data);
    return token;
}

static finline Token Token_left_fd(Lexer* lexer, Tokentype type, int32 fd)
{
    Token token;
    Token_init(&token);
    TF(&token).fd_left = fd;
    TF(&token).read = type == TOKEN_FDREAD;
    token.type = type;
    return token;
}


/* Code crimes submission function. */
Token Lexer_next(Lexer* lexer)
{
    int32 c, fd;
    Tokentype ttype;
    lexer_skip_ws(lexer);
    Token_init(&lexer->curr);
    switch((c = peek(lexer, 0))) {
        case '<': {
            advance(lexer);
            TF(&lexer->curr).fd_left = 0;
            lexer->curr.type = TOKEN_FDREAD;
            goto l_check_right;
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
            l_check_right:
                if(peek(lexer, 0) == '&') {
                    advance(lexer);
                    if((c = peek(lexer, 0)) == '-') { // close file descriptor ?
                        advance(lexer);
                        if(!isspace(peek(lexer, 1))) goto l_err_fd_invalid;
                        lexer->curr.type = TOKEN_FDCLOSE;
                    } else if(isdigit(c)) goto l_fd_right;
                    else goto l_err_fd_invalid;
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
            fd = fd_left(TS(&lexer->curr));
            if(BIGFD(fd)) goto l_err_fd_overflow;
            if(!BADFD(fd)) {
                if((c = peek(lexer, 0)) == '>' || c == '<') {
                    advance(lexer);
                    lexer->curr = Token_left_fd(lexer, TOKEN_FDWRITE, fd);
                    goto l_fd_write; // check for append flag + right side fd
                } else if(c == '<') {
                    advance(lexer);
                    lexer->curr = Token_left_fd(lexer, TOKEN_FDREAD, fd);
                    goto l_check_right; // check for right side fd
                }
            }
            return lexer->curr; // string
        }
    }
    advance(lexer);
    lexer->curr.type = ttype;
    return lexer->curr;
l_fd_right:
    lexer->curr = Lexer_string(lexer);
    if(!isspace(peek(lexer, 0))) goto l_err_fd_invalid;
    fd = fd_right(TS(&lexer->curr));
    if(BIGFD(fd)) goto l_err_fd_overflow;
    if(BADFD(fd)) goto l_err_fd_invalid;
    TF(&lexer->curr).fd_right = fd;
    return lexer->curr;
l_err_fd_overflow:
    lexer->curr = Token_error(lexerrors[ERR_FDOVF], TS(&lexer->curr));
    goto l_defer;
l_err_fd_invalid:
    lexer->curr = Token_error(lexerrors[ERR_FDINV], TS(&lexer->curr));
l_defer:
    Buffer_free(&lexer->curr.u.contents, NULL);
    return lexer->curr;
}
