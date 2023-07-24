#ifdef AN_DEBUG
    #include <assert.h>
#endif
#include "ashe_string.h"
#include "ashe_utils.h"
#include "lexer.h"
#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define cmd_is_null(command) (command.env == NULL || command.argv == NULL)

///*************  SHELL-COMMAND  ******************///
static command_t command_new(void);
static int parse_command(lexer_t *lexer, command_t *out, bool *set_env);
static void command_drop(command_t *cmd);
///----------------------------------------------///

///***************  PIPELINE  *******************///
static pipeline_t pipeline_new(void);
static int parse_pipeline(lexer_t *lexer, pipeline_t *out, bool *set_env);
static void pipeline_drop(pipeline_t *pipeline);
///----------------------------------------------///

///*************  CONDITIONAL  ******************///
static conditional_t conditional_new(void);
static int parse_conditional(lexer_t *lexer, conditional_t *out, bool *set_env);
void conditional_drop(conditional_t *cond);
///----------------------------------------------///

static int _parse_commandline(lexer_t *lexer, commandline_t *cmdline, bool *set_env);

int parse_commandline(const byte *line, commandline_t *out, bool *set_env)
{
    lexer_t lexer = lexer_new(line, strlen(line));
    token_t token = lexer_next(&lexer);

    /// Empty cmdline
    if(token.type == EOL_TOKEN)
        return SUCCESS;

    return _parse_commandline(&lexer, out, set_env);
}

static int _parse_commandline(lexer_t *lexer, commandline_t *cmdline, bool *set_env)
{
    conditional_t cond;
    conditional_t *lastcond;
    token_t token = lexer->token;
    int status;

    while(1) {
        if((token.type & (WORD_TOKEN | KVPAIR_TOKEN)) == 0) {
            if(token.type == NAT_TOKEN)
                pwarn("invalid syntax '%s'", string_ref(token.contents));
            else if(__glibc_unlikely(token.type != OOM_TOKEN))
                pwarn(
                    "expected '&&' or '||' instead got '%s'", string_ref(token.contents));
            else
                string_drop(token.contents);

            return FAILURE;
        }

        if(is_null((cond = conditional_new()).pipelines))
            return FAILURE;

        status = parse_conditional(lexer, &cond, set_env);
        token = lexer->token;

        if(status == FAILURE || __glibc_unlikely(!vec_push(cmdline->conditionals, &cond)))
        {
            conditional_drop(&cond);
            return FAILURE;
        }

        if(token.type & (FG_TOKEN | BG_TOKEN)) {
            lastcond = vec_back(cmdline->conditionals);
            if(token.type == BG_TOKEN)
                lastcond->is_background = true;
            free(token.contents);
            if((token = lexer_next(lexer)).type == EOL_TOKEN)
                break;
        } else {
            break;
        }
    };

    return SUCCESS;
}

static int parse_command(lexer_t *lexer, command_t *command, bool *set_env)
{
    token_t token = lexer->token;
    bool env = false;

    if(token.type == KVPAIR_TOKEN) {
        env = true;
    }

    while(1) {
        if(env && token.type != KVPAIR_TOKEN) {
            env = false;
        }
        if(__glibc_unlikely(
               !vec_push((env) ? command->env : command->argv, token.contents)))
        {
            return FAILURE;
        }
        free(token.contents);
        token = lexer_next(lexer);
        if(!(token.type & (WORD_TOKEN | KVPAIR_TOKEN | REDIROP_TOKEN))) {
            if(env) {
                *set_env = true;
                byte *var;
                vec_t *vars = command->env;
                size_t len = vec_len(command->env);
                while(len--) {
                    if(__glibc_unlikely(
                           putenv((var = string_slice(vec_index(vars, len), 0)))
                           != SUCCESS))
                    {
                        ATOMIC_PRINT({
                            pwarn("failed setting env var '%s'", var);
                            perr();
                        });
                        return FAILURE;
                    }
                }
            }
            break;
        }
    }

    return SUCCESS;
}

static int parse_pipeline(lexer_t *lexer, pipeline_t *pipeline, bool *set_env)
{
    token_t token = lexer->token;
    command_t command;
    int status;

    while(1) {
        if(__glibc_unlikely(cmd_is_null((command = command_new()))))
            return FAILURE;

        status = parse_command(lexer, &command, set_env);
        token = lexer->token;

        if(status == FAILURE || __glibc_unlikely(!vec_push(pipeline->commands, &command)))
        {
            command_drop(&command);
            return FAILURE;
        }

        if(token.type == PIPE_TOKEN) {
            free(token.contents);
            token = lexer_next(lexer);
        } else {
            break;
        }
    }

    return SUCCESS;
}

static int parse_conditional(lexer_t *lexer, conditional_t *cond, bool *set_env)
{
    pipeline_t pipeline;
    pipeline_t *lastpipe;
    token_t token = lexer->token;
    int status;

    while(1) {
        pipeline = pipeline_new();
        if(__glibc_unlikely(is_null(pipeline.commands)))
            return FAILURE;

        status = parse_pipeline(lexer, &pipeline, set_env);
        token = lexer->token;

        if(status == FAILURE || __glibc_unlikely(!vec_push(cond->pipelines, &pipeline))) {
            pipeline_drop(&pipeline);
            return FAILURE;
        }

        if(token.type & (AND_TOKEN | OR_TOKEN)) {
            lastpipe = vec_back(cond->pipelines);
            if(token.type == AND_TOKEN)
                lastpipe->connection = ASH_AND;
            else
                lastpipe->connection = ASH_OR;
            free(token.contents);
            token = lexer_next(lexer);
        } else {
            break;
        }
    }

    return SUCCESS;
}

static conditional_t conditional_new(void)
{
    return (conditional_t){
        .pipelines = vec_new(sizeof(pipeline_t)),
        .is_background = false,
    };
}

void conditional_drop(conditional_t *cond)
{
    if(is_some(cond))
        vec_drop(&cond->pipelines, (FreeFn) pipeline_drop);
}

static pipeline_t pipeline_new(void)
{
    return (pipeline_t){
        .commands = vec_new(sizeof(command_t)),
        .connection = ASH_NONE,
    };
}

static void pipeline_drop(pipeline_t *pipeline)
{
    if(is_some(pipeline))
        vec_drop(&pipeline->commands, (FreeFn) command_drop);
}

static command_t command_new(void)
{
    command_t cmd = {.env = NULL, .argv = NULL};
    vec_t *envp = vec_new(sizeof(string_t *));
    if(__glibc_unlikely(is_null(envp)))
        return cmd;
    vec_t *argv = vec_new(sizeof(string_t *));
    if(__glibc_unlikely(is_null(argv)))
        return cmd;
    cmd.env = envp;
    cmd.argv = argv;
    return cmd;
}

static void command_drop(command_t *cmd)
{
    if(is_some(cmd)) {
        vec_drop(&cmd->argv, (FreeFn) string_drop_inner);
        vec_drop(&cmd->env, (FreeFn) string_drop_inner);
    }
}
