#include "ashe_utils.h"
#include "lexer.h"
#include "parser.h"
#include "shell.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define STRING_TOKEN (TOKEN_KVPAIR | TOKEN_WORD)
#define IS_REDIR(type) ((type) & (STDIN_TOKEN | FDSRC_TOKEN | FDDEST_TOKEN))
#define IS_STRING(type) ((type) & STRING_TOKEN)



#define WARN_WINPUT(lexer, block) \
    do {  \
        block; \
        fprint_warning("input: '%s'", (lexer)->start); \
    } while(0); \




#define jump_out(code) \
    do { \
        ashe.sh_buf.res = code; \
        longjmp(ashe.sh_buf.buf, 1); \
    } while(0)

#define advance_or_jmpout(lexer, cmd) \
    if(advance(lexer) != 0) { \
        if(cmd) Command_free(cmd); \
        jump_out(-1); \
    } \




#define ERR_EXPSTRING 0
static const char* errors[] = {
    "syntax error, expected string instead got '%s'",
};




int32 advance(Lexer* lexer) {
    lexer->prev = lexer->curr;
    lexer->curr = Lexer_next(lexer);
    if(lexer->curr.type == TOKEN_ERROR) {
        fprint_error(TS(&lexer->curr));
        return -1;
    }
    return 0;
}

static int32 expect(Lexer* lexer, uint16 types, const char* errmsg, ...)
{
    va_list argp;
    if(types & lexer->curr.type) {
        Lexer_next(lexer);
        return 0;
    }
    va_start(argp, errmsg);
    WARN_WINPUT(lexer, vfprint_warning(errmsg, argp));
    va_end(argp);
    return -1;
}





/* =========== AST =========== */

void Redirection_init(Redirection* rd)
{
    rd->append = 0;
    Buffer_init(&rd->file);
}

void Redirection_free(Redirection* rd)
{
    Buffer_free(&rd->file, NULL);
    Redirection_init(rd);
}



void Command_init(Command* cmd)
{
    ArrayBuffer_init(&cmd->argv);
    ArrayBuffer_init(&cmd->env);
    Redirection_init(&cmd->rd[0]);
    Redirection_init(&cmd->rd[1]);
    Redirection_init(&cmd->rd[2]);

}

void Buffer_free_wrapped(Buffer* buf) 
{
    Buffer_free(buf, NULL);
}

void Command_free(Command* cmd)
{
    ArrayBuffer_free(&cmd->argv, (FreeFn) Buffer_free_wrapped);
    ArrayBuffer_free(&cmd->env, (FreeFn) Buffer_free_wrapped);
    Redirection_free(&cmd->rd[0]); // Redirection_init in here
    Redirection_free(&cmd->rd[1]);
    Redirection_free(&cmd->rd[2]);
    ArrayBuffer_init(&cmd->argv);
    ArrayBuffer_init(&cmd->env);
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

int32 redir_stdin(Lexer* lexer, Command* cmd)
{
    advance_or_jmpout(lexer, cmd);
    const char* got = Token_tostr(&lexer->curr);
    if(expect(lexer, STRING_TOKEN, errors[ERR_EXPSTRING], got) == -1)
        return -1;
    const char* str = lexer->curr.u.contents.data;
    memmax len = lexer->curr.u.contents.len - 1;
    Buffer_push_str(&cmd->rd[STDIN_FILENO].file, str, len);
    return 0;
}

int32 fdsrc(Lexer* lexer, Command* cmd)
{
    return 0;
}

void command(Lexer* lexer, Command* cmd)
{
    for(;;) {
        advance_or_jmpout(lexer, cmd);
        ArrayBuffer* target;
        switch(lexer->curr.type) {
            case TOKEN_WORD: {
                target = &cmd->argv;
                goto t_push;
            }
            case TOKEN_KVPAIR: {
                target = &cmd->env;
            t_push:
                ArrayBuffer_push(target, lexer->curr.u.contents);
                break;
            }
            case STDIN_TOKEN: {
                if(redir_stdin(lexer, cmd) == -1)
                    goto l_err;
                break;
            }
            case FDSRC_TOKEN: {
                if(fdsrc(lexer, cmd) == -1)
                    goto l_err;
                break;
            }
            case FDDEST_TOKEN: {
                if(fddest(lexer, cmd) == -1)
                    goto l_err;
                break;
            }
            default:
                return;
        }
    }

l_err:
    Command_free(cmd);
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
    Lexer lexer;
    Conditional cond;
    AsheJmpBuf* jmpbuf = &ashe.sh_buf;
    Lexer_init(&lexer, SHELL_BUFFER(&ashe));
    jmpbuf->res = 0;
    if(setjmp(jmpbuf->buf) == 0) { // buffer setter ?
        for(;;) {
            Conditional_init(&cond);
            conditional(&lexer, &cond);
            ArrayConditional_push(conds, cond);
            if(!(lexer.curr.type & (SEPARATOR_TOKEN | TOKEN_BG))) break;
        }
        if(lexer.curr.type != TOKEN_EOF) {
            const char* got = Token_tostr(&lexer.curr);
            WARN_WINPUT(&lexer, fprint_warning(errors[ERR_EXPSTRING], got));
            jmpbuf->res = -1;
        }
    } else { // jumped out
        WARN_WINPUT(&lexer, fprint_warning(lexer.curr.u.contents.data));
    }
    return jmpbuf->res;
}
