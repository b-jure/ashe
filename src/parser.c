#include "ashe_utils.h"
#include "lexer.h"
#include "parser.h"
#include "shell.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define STRING_TOKEN (TOKEN_KVPAIR | TOKEN_WORD)
#define IS_STRING(type) ((type) & STRING_TOKEN)



/* Run the 'block' and emit warning with user input. */
#define WARN_WINPUT(lexer, block) \
    do {  \
        block; \
        fprint_warning("input: '%s'", (lexer)->start); \
    } while(0); \




/* Jump out of code into 'AsheJmpBuf'.
 * Additionally set the buffer res to 'code'. */
#define jump_out(code) \
    do { \
        ashe.sh_buf.res = code; \
        longjmp(ashe.sh_buf.buf, 1); \
    } while(0)

/* Advance to next token, in case of errors
 * execute 'block' and jump out with -1 code. */
#define advance_or_jmpout(lexer, block) \
    if(advance(lexer) != 0) { \
        block; \
        jump_out(-1); \
    } \




/* Parsing errors */
#define ERR_EXPSTRING 0
static const char* perrors[] = {
    "syntax error, expected string instead got '%s'",
};




/* Advance to the next token.
 * If token is 'TOKEN_ERROR' [atoken.h] return -1. */
int32 advance(Lexer* lexer) {
    lexer->prev = lexer->curr;
    lexer->curr = Lexer_next(lexer);
    if(lexer->curr.type == TOKEN_ERROR) {
        fprint_warning(TS(&lexer->curr));
        return -1;
    }
    return 0;
}

/* If current token does not match any of the 'types' (bitmask)
 * output a warning to the user and return -2.
 * Otherwise advance to the next token.
 * In case advance fails it returns -1.
 * If all is good return 0. */
static int32 expect(Lexer* lexer, uint16 types, const char* errmsg, ...)
{
    va_list argp;
    if(types & lexer->curr.type)
        return advance(lexer);
    va_start(argp, errmsg);
    WARN_WINPUT(lexer, vfprint_warning(errmsg, argp));
    va_end(argp);
    return -2;
}





/* =========== AST (storage) =========== */

void Redirection_init(FDContext* rd)
{
    rd->fd_left = -1;
    rd->fd_left = -1;
    rd->write = 0;
    rd->append = 0;
    Buffer_init(&rd->file);
}

void Redirection_free(FDContext* rd)
{
    Buffer_free(&rd->file, NULL);
    Redirection_init(rd);
}


void Command_init(Command* cmd)
{
    ArrayBuffer_init(&cmd->argv);
    ArrayBuffer_init(&cmd->env);
    ArrayFDContext_init(&cmd->fdcs);
}

void Buffer_free_wrapped(Buffer* buf) { Buffer_free(buf, NULL); }
void Command_free(Command* cmd)
{
    ArrayBuffer_free(&cmd->argv, (FreeFn) Buffer_free_wrapped);
    ArrayBuffer_free(&cmd->env, (FreeFn) Buffer_free_wrapped);
    ArrayFDContext_free(&cmd->fdcs, (FreeFn) Redirection_free);
    Command_init(cmd);
}


void Pipeline_init(Pipeline* pip)
{
    ArrayCommand_init(&pip->commands);
    pip->connection = CON_NONE;
}

void Pipeline_free(Pipeline* pip)
{
    ArrayCommand_free(&pip->commands, (FreeFn) Command_free);
    Pipeline_init(pip);
}


void Conditional_init(Conditional* cond)
{
    ArrayPipeline_init(&cond->pipelines);
    cond->is_background = 0;
}

void Conditional_free(Conditional* cond)
{
    ArrayPipeline_free(&cond->pipelines, (FreeFn) Pipeline_free);
    Conditional_init(cond);
}






/* ============ PARSING ============ */

void command(Lexer* lexer, Command* cmd)
{
    FDContext fdc;
    ArrayBuffer* target;
    for(;;) {
        Redirection_init(&fdc);
        advance_or_jmpout(lexer, Command_free(cmd));
        switch(lexer->curr.type) {
            case TOKEN_WORD: {
                target = &cmd->argv;
                goto t_push_buffer;
            }
            case TOKEN_KVPAIR: {
                target = &cmd->env;
            t_push_buffer:
                ArrayBuffer_push(target, lexer->curr.u.contents);
                break;
            }
            case TOKEN_FDREAD: {
                fdc.write = 0;
                fdc.fd_left = TF(&lexer->curr).fd_left;
                fdc.append = 0;
                goto l_expect_string;
            }
            case TOKEN_FDWRITE: {
                fdc.write = 1;
                fdc.fd_left = TF(&lexer->curr).fd_left;
                fdc.append = TF(&lexer->curr).append;
            l_expect_string:
                fdc.close = 0;
                advance_or_jmpout(lexer, Command_free(cmd));
                if(!IS_STRING(lexer->curr.type)) goto l_err_expect_string;
                fdc.file = TB(&lexer->curr);
                goto l_push_fdcontext;
            }
            case TOKEN_FDREDIRECT: {
                fdc.write = TF(&lexer->curr).write;
                fdc.fd_left = TF(&lexer->curr).fd_left;
                fdc.fd_right = TF(&lexer->curr).fd_right;
                fdc.append = TF(&lexer->curr).append;
                fdc.close = 0;
                goto l_push_fdcontext;
            }
            case TOKEN_FDCLOSE: {
                fdc.write = 0;
                fdc.append = 0;
                fdc.fd_left = TF(&lexer->curr).fd_left;
                fdc.fd_right = -1;
                fdc.close = 1;
            l_push_fdcontext:
                ArrayFDContext_push(&cmd->fdcs, fdc); 
                break;
            }
            default:
                return;
        }
    }
l_err_expect_string:
    Command_free(cmd);
    WARN_WINPUT(lexer, fprint_warning(perrors[ERR_EXPSTRING], Token_tostr(&lexer->curr)));
    jump_out(-1);
}


void pipeline(Lexer* lexer, Pipeline* pip)
{
    for(;;) {
        Command cmd;
        Command_init(&cmd);
        command(lexer, &cmd); // advance in here
        ArrayCommand_push(&pip->commands, cmd);
        if(!(lexer->curr.type & TOKEN_PIPE)) break;
    }
}

void conditional(Lexer* lexer, Conditional* cond)
{
    for(;;) {
        Pipeline pip;
        Pipeline_init(&pip);
        pipeline(lexer, &pip); // advance in here
        if(lexer->prev.type == TOKEN_AND) pip.connection = CON_AND;
        else if(lexer->prev.type == TOKEN_OR) pip.connection = CON_OR;
        ArrayPipeline_push(&cond->pipelines, pip);
        if(!(lexer->curr.type & (TOKEN_AND | TOKEN_OR))) break;
    }
}

int32 parse(ArrayConditional* conds)
{
    Lexer* lexer = &ashe.sh_lexer;
    Conditional cond;
    AsheJmpBuf* jmpbuf = &ashe.sh_buf;
    Lexer_init(lexer, SHELL_BUFFER(&ashe));
    jmpbuf->res = 0;
    if(setjmp(jmpbuf->buf) == 0) { // buffer setter ?
        for(;;) {
            Conditional_init(&cond);
            conditional(lexer, &cond);
            ArrayConditional_push(conds, cond);
            if(!(lexer->curr.type & (TOKEN_SEPARATOR | TOKEN_BG))) break;
        }
        if(lexer->curr.type != TOKEN_EOF) {
            const char* got = Token_tostr(&lexer->curr);
            WARN_WINPUT(lexer, fprint_warning(perrors[ERR_EXPSTRING], got));
            jmpbuf->res = -1;
        }
    } else { // jumped out
        // do nothing, warning/error should already be reported
    }
    return jmpbuf->res;
}
