#ifdef AN_DEBUG
    #include <assert.h>
#endif
#include "asheutils.h"
#include "lexer.h"
#include "parser.h"
#include "string.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

///*************  SHELL-COMMAND  ******************///
static command_t command_new(void);
static void command_drop(command_t *cmd);
static int parse_command(lexer_t *lexer, command_t *out);
///----------------------------------------------///

///***************  PIPELINE  *******************///
static pipeline_t pipeline_new(void);
static void pipeline_drop(pipeline_t *pipeline);
static int parse_pipeline(lexer_t *lexer, pipeline_t *out);
///----------------------------------------------///

///*************  CONDITIONAL  ******************///
static conditional_t conditional_new(void);
static int parse_conditional(lexer_t *lexer, conditional_t *out);
void conditional_drop(conditional_t *cond);
///----------------------------------------------///

int parse_commandline(const byte *line, commandline_t *out)
{
    token_t token;
    lexer_t lexer = lexer_new(line, strlen(line));
    conditional_t cond;
    command_t command;
    pipeline_t pipeline;
    bool env = false;

    if(is_null(cond.pipelines)) {
        return FAILURE;
    }

    token = lexer_next(&lexer);

    while(1) {
        if((token.type & (WORD_TOKEN | KVPAIR_TOKEN)) == 0) {
            goto err;
        }
        cond = conditional_new();
        if(is_null(cond.pipelines)) {
            goto err;
        }
        while(1) {
            pipeline = pipeline_new();
            if(is_null(pipeline.commands)) {
                goto cond_err;
            }
            while(1) {
                command = command_new();
                if(is_null(command.env) || is_null(command.args)) {
                    goto pipe_err;
                }
                if(token.type == KVPAIR_TOKEN) {
                    env = true;
                }
                while(1) {
                    if(env && token.type != KVPAIR_TOKEN) {
                        env = false;
                    }
                    if(!vec_push((env) ? command.env : command.args, token.contents))
                    {
                        goto cmd_err;
                    }
                    free(token.contents);
                    token = lexer_next(&lexer);
                    if(!(token.type & (WORD_TOKEN | KVPAIR_TOKEN | REDIROP_TOKEN))) {
                        break;
                    }
                }
                if(!vec_push(pipeline.commands, &command)) {
                    goto cmd_err;
                }
                if(token.type == PIPE_TOKEN) {
                    free(token.contents);
                    token = lexer_next(&lexer);
                } else {
                    break;
                }
            }

            if(token.type & (AND_TOKEN | OR_TOKEN)) {
                if(token.type == AND_TOKEN) {
                    pipeline.connection = ASH_AND;
                } else {
                    pipeline.connection = ASH_OR;
                }

                if(!vec_push(cond.pipelines, &pipeline)) {
                    goto pipe_err;
                } else {
                    free(token.contents);
                    token = lexer_next(&lexer);
                }
            } else {
#ifdef ASHE_DEBUG
                assert(pipeline.connection == ASH_NONE);
#endif
                break;
            }
        }

        if(token.type & (FG_TOKEN | BG_TOKEN)) {
            if(token.type == BG_TOKEN) {
                cond.is_background = true;
            }

            if(!vec_push(out->conditionals, &cond)) {
                goto cond_err;
            } else {
                free(token.contents);
                token = lexer_next(&lexer);
            }
        } else {
            break;
        }
    };

    switch(token.type) {
        case EOL_TOKEN:
            break;
        case NAT_TOKEN:
            INVALID_SYNTAX_ERR(string_ref(token.contents));
            goto err;
        default:
            EXPECTED_ANDOR_OR_EOL_ERR(string_ref(token.contents));
            goto err;
            break;
    }

    return SUCCESS;

cmd_err:
    command_drop(&command);
pipe_err:
    pipeline_drop(&pipeline);
cond_err:
    conditional_drop(&cond);
err:
    free(token.contents);
    return FAILURE;
}

commandline_t commandline_new(void)
{
    return (commandline_t){.conditionals = vec_new(sizeof(conditional_t))};
}

void commandline_drop(commandline_t *cmdline)
{
    if(is_some(cmdline)) {
        vec_drop(&cmdline->conditionals, (FreeFn) conditional_drop);
    }
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
    if(is_some(cond)) {
        vec_drop(&cond->pipelines, (FreeFn) pipeline_drop);
    }
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
    if(is_some(pipeline)) {
        vec_drop(&pipeline->commands, (FreeFn) command_drop);
    }
}

static command_t command_new(void)
{
    return (command_t){
        .env = vec_new(sizeof(string_t *)),
        .args = vec_new(sizeof(string_t *)),
    };
}

static void command_drop(command_t *cmd)
{
    if(is_some(cmd)) {
        vec_drop(&cmd->env, (FreeFn) free);
        vec_drop(&cmd->args, (FreeFn) free);
    }
}
