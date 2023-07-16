#ifdef AN_DEBUG
    #include <assert.h>
#endif
#define _DEFAULT_SOURCE
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

#define cmd_is_null(command) (command.env == NULL || command.args == NULL)

static void print_token(token_t *token)
{
    switch(token->type) {
        case REDIROP_TOKEN:
            printf(
                "TOKEN: %s <> '%s'\n", "REDIRECTION", string_ref(token->contents));
            break;
        case WORD_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "WORD", string_ref(token->contents));
            break;
        case KVPAIR_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "K/V PAIR", string_ref(token->contents));
            break;
        case PIPE_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "PIPE", string_ref(token->contents));
            break;
        case AND_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "AND", string_ref(token->contents));
            break;
        case OR_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "OR", string_ref(token->contents));
            break;
        case BG_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "BACKGROUND", string_ref(token->contents));
            break;
        case FG_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "FOREGROUND", string_ref(token->contents));
            break;
        case NAT_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "NAT", "NULL");
            break;
        case EOL_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "EOL", "NULL");
            break;
        case OOM_TOKEN:
            printf("TOKEN: %s <> '%s'\n", "OOM", "NULL");
            break;
    }
}

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
    int status = SUCCESS;
    lexer_t lexer = lexer_new(line, strlen(line));
    token_t token = lexer_next(&lexer);
    printf("\n\n[first] ");
    print_token(&token);

    status = _parse_commandline(&lexer, out, set_env);
    token = lexer.token;

    if(status == FAILURE) {
        goto err;
    }

    switch(token.type) {
        case EOL_TOKEN:
            break;
        case NAT_TOKEN:
            INVALID_SYNTAX_ERR(string_ref(token.contents));
        case OOM_TOKEN:
            goto err;
        default:
            EXPECTED_ANDOR_OR_EOL_ERR(string_ref(token.contents));
            goto err;
            break;
    }

    return SUCCESS;

err:
    commandline_clear(out);
    string_drop(token.contents);
    return FAILURE;
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
        if(!vec_push((env) ? command->env : command->args, token.contents)) {
            return FAILURE;
        }
        free(token.contents);
        token = lexer_next(lexer);
        printf("\n\n[command] ");
        print_token(&token);
        if(!(token.type & (WORD_TOKEN | KVPAIR_TOKEN | REDIROP_TOKEN))) {
            if(env) {
                *set_env = true;
                byte *var;
                vec_t *vars = command->env;
                size_t len = vec_len(command->env);
                while(len--) {
                    if(putenv((var = string_slice(vec_index(vars, len), 0)))
                       != SUCCESS)
                    {
                        FAILED_SETTING_ENVVAR(var);
                        perror("putenv");
                    }
                }
            }
            printf("Breaking out of COMMAND parser\n");
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
        if(cmd_is_null((command = command_new()))) {
            return FAILURE;
        }

        status = parse_command(lexer, &command, set_env);
        token = lexer->token;

        if(status == FAILURE || !vec_push(pipeline->commands, &command)) {
            command_drop(&command);
            return FAILURE;
        }

        if(token.type == PIPE_TOKEN) {
            free(token.contents);
            token = lexer_next(lexer);
            printf("\n\n[pipeline] ");
            print_token(&token);
        } else {
            printf("Breaking out of PIPELINE parser\n");
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
        if(is_null(pipeline.commands)) {
            return FAILURE;
        }

        status = parse_pipeline(lexer, &pipeline, set_env);
        token = lexer->token;

        if(status == FAILURE || !vec_push(cond->pipelines, &pipeline)) {
            pipeline_drop(&pipeline);
            return FAILURE;
        }

        if(token.type & (AND_TOKEN | OR_TOKEN)) {
            lastpipe = vec_back(cond->pipelines);
            if(token.type == AND_TOKEN) {
                lastpipe->connection = ASH_AND;
            } else {
                lastpipe->connection = ASH_OR;
            }
            free(token.contents);
            token = lexer_next(lexer);
            printf("\n\n[conditional] ");
            print_token(&token);
        } else {
            printf("Breaking out of CONDITIONAL parser\n");
            break;
        }
    }

    return SUCCESS;
}

static int _parse_commandline(lexer_t *lexer, commandline_t *cmdline, bool *set_env)
{
    conditional_t cond;
    conditional_t *lastcond;
    token_t token = lexer->token;
    int status;

    while(1) {
        if((token.type & (WORD_TOKEN | KVPAIR_TOKEN)) == 0) {
            return FAILURE;
        }
        cond = conditional_new();
        if(is_null(cond.pipelines)) {
            return FAILURE;
        }

        status = parse_conditional(lexer, &cond, set_env);
        token = lexer->token;

        if(status == FAILURE || !vec_push(cmdline->conditionals, &cond)) {
            conditional_drop(&cond);
            return FAILURE;
        }

        if(token.type & (FG_TOKEN | BG_TOKEN)) {
            lastcond = vec_back(cmdline->conditionals);
            if(token.type == BG_TOKEN) {
                lastcond->is_background = true;
            }
            free(token.contents);
            token = lexer_next(lexer);
            printf("\n\n[commandline] ");
            print_token(&token);
        } else {
            printf("Breaking out of COMMANDLINE parser\n");
            break;
        }
    };

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
    if(is_some(cond)) {
        printf("Dropping conditional...\n");
        vec_drop(&cond->pipelines, (FreeFn) pipeline_drop);
        printf("done! [conditional]");
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
        printf("Dropping pipeline...\n");
        vec_drop(&pipeline->commands, (FreeFn) command_drop);
        printf("done! [pipeline]\n");
    }
}

static command_t command_new(void)
{
    command_t cmd = {.env = NULL, .args = NULL};
    vec_t *envp = vec_new(sizeof(string_t *));
    if(is_null(envp)) {
        return cmd;
    }
    vec_t *argv = vec_new(sizeof(string_t *));
    if(is_null(argv)) {
        return cmd;
    }
    cmd.env = envp;
    cmd.args = argv;
    return cmd;
}

static void command_drop(command_t *cmd)
{
    if(is_some(cmd)) {
        /*dbg*/ size_t lena = vec_len(cmd->args);
        /*dbg*/ size_t lene = vec_len(cmd->env);
        /*dbg*/ printf(
            "Dropping command with %ld args and %ld env vars\n", lena, lene);
        /*dbg*/ printf("ARGS: ");
        /*dbg*/ while(lena--)
        {
            /*dbg*/ printf("%s, ", string_ref(vec_index(cmd->args, lena)));
        /*dbg*/}
        /*dbg*/ printf("NULL\n");
        /*dbg*/ printf("ENVS: ");
        /*dbg*/ while(lene--)
        {
            /*dbg*/ printf("%s, ", string_ref(vec_index(cmd->env, lene)));
        /*dbg*/}
        /*dbg*/ printf("NULL\n");
        /*dbg*/ printf("Dropping command args...\n");
        vec_drop(&cmd->args, (FreeFn) string_drop_inner);
        /*dbg*/ printf("done! [command->args]\n");
        /*dbg*/ printf("Dropping command envs...\n");
        vec_drop(&cmd->env, (FreeFn) string_drop_inner);
        /*dbg*/ printf("done! [command->env]\n");
        /*dbg*/ printf("done! [command]\n");
    }
}
