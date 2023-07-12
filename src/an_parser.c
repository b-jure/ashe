#ifdef AN_DEBUG
    #include <assert.h>
#endif
#include "an_lexer.h"
#include "an_parser.h"
#include "an_string.h"
#include "an_vec.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

///*************  SHELL-COMMAND  ******************///
typedef struct {
    vec_t *args; /* Command name and arguments */
    vec_t *env;  /* Program environmental variables */
} command_t;

static command_t command_new(void);
static void command_drop(command_t *cmd);
static int parse_command(lexer_t *lexer, command_t *out);
///----------------------------------------------///

///*************  PIPELINE  ******************///
typedef struct {
    command_t command1;
    command_t command2;
} tuple_t;

static void tuple_drop(tuple_t *tuple);
static tuple_t tuple_new(void);

typedef union {
    command_t command;
    tuple_t pipe;
} pipe_t;

typedef enum {
    SINGLETON, /* pipe with single command */
    TUPLE,     /* normal pipe with left and right command */
} pipetype_t;

typedef struct {
    pipe_t inner;    /* pipe command/s */
    pipetype_t type; /* type of pipeline */
    bool is_and;     /* Pipeline is connected with '&&' */
} pipeline_t;

static pipeline_t pipeline_new(pipetype_t type);
static void pipeline_drop(pipeline_t *pipeline);
static int parse_pipeline(lexer_t *lexer, pipeline_t *out);
///----------------------------------------------///

///*************  CONDITIONAL  ******************///
typedef struct {
    vec_t *pipelines;   /* collection of pipelines */
    bool is_background; /* proccess is run in foreground otherwise background */
} conditional_t;

static conditional_t conditional_new(void);
static int parse_conditional(lexer_t *lexer, conditional_t *out);
static void conditional_drop(conditional_t *cond);
///----------------------------------------------///

///*************  COMMANDLINE  ******************///
struct commandline_t {
    vec_t *conditionals;
};

static void commandline_drop(commandline_t *cmdline);
///----------------------------------------------///

commandline_t commandline_new(void)
{
    return (commandline_t){.conditionals = vec_new(sizeof(conditional_t))};
}

int parse_commandline(const byte *line, size_t len, commandline_t *out)
{
    lexer_t lexer = lexer_new(line, len);
    token_t token = lexer_next(&lexer);
    conditional_t cond = conditional_new();

#ifdef DEBUG
    assert(token.type & (KVPAIR_TOKEN | WORD_TOKEN));
#endif

    if(is_null(cond.pipelines)) {
        return -1;
    }

    do {
        if(token.type == EOL_TOKEN) {
            break;
        } else if(!(token.type & (KVPAIR_TOKEN | WORD_TOKEN))) {
            EXPECTED_STRING_ERR(string_ref(token.contents));
            goto err;
        } else if(
            parse_conditional(&lexer, &cond) == -1
            || !vec_push(out->conditionals, &cond))
        {
            return -1;
        }
    } while((token = lexer_next(&lexer)).type & (FG_TOKEN | BG_TOKEN));

    return 0;

err:
    conditional_drop(&cond);
    return -1;
}

static int parse_conditional(lexer_t *lexer, conditional_t *out)
{
    pipeline_t pipeline;
    token_t token = lexer_peek(lexer);

    return 0;
}

static conditional_t conditional_new(void)
{
    return (conditional_t){
        .pipelines = vec_new(sizeof(pipeline_t)),
        .is_background = false,
    };
}

static void conditional_drop(conditional_t *cond)
{
    if(is_some(cond)) {
        vec_drop(&cond->pipelines, (FreeFn) pipeline_drop);
    }
}

static int parse_pipeline(lexer_t *lexer, pipeline_t *out)
{
    token_t token = lexer_next(lexer);
    return 0;
}

static pipeline_t pipeline_new(pipetype_t type)
{
    pipeline_t pipeline;

    if(type == SINGLETON) {
        pipeline.type = SINGLETON;
        pipeline.inner.command = command_new();
    } else {
        pipeline.type = TUPLE;
        pipeline.inner.pipe = tuple_new();
    }

    return pipeline;
}

static void pipeline_drop(pipeline_t *pipeline)
{
    if(is_some(pipeline)) {
        if(pipeline->type == SINGLETON) {
            tuple_drop(&pipeline->inner.pipe);
        } else {
            command_drop(&pipeline->inner.command);
        }
    }
}

static void tuple_drop(tuple_t *tuple)
{
    if(is_some(tuple)) {
        command_drop(&tuple->command1);
        command_drop(&tuple->command2);
    }
}

static int parse_command(lexer_t *lexer, command_t *out)
{
    bool start = true;
    bool env = false;
    token_t token = lexer_peek(lexer);

#ifdef DEBUG
    assert(token.type & (KVPAIR_TOKEN | WORD_TOKEN));
#endif

    if(token.type == KVPAIR_TOKEN) {
        env = true;
    }

    do {
        if(!start) {
            if(is_null((token = lexer_next(lexer)).contents)) {
                return -1;
            }
            if(env && token.type != KVPAIR_TOKEN) {
                env = false;
            }
        } else {
            start = false;
        }

        if(token.type == REDIROP_TOKEN) {
            if(lexer_next(lexer).contents == NULL) {
                free(token.contents);
                return -1;
            }

            if(!vec_push(out->args, token.contents)) {
                goto push_err;
            }

            free(token.contents);
            if(is_null((token = lexer_peek(lexer)).contents)) {
                return -1;
            } else if(token.type & (WORD_TOKEN | KVPAIR_TOKEN)) {
                EXPECTED_STRING_ERR(string_ref(token.contents));
                free(token.contents);
                return -1;
            }

            lexer_next(lexer);
        }

        if(!vec_push((env) ? out->env : out->args, token.contents)) {
            goto push_err;
        }

        free(token.contents);
    } while((token = lexer_peek(lexer)).type
            & (WORD_TOKEN | REDIROP_TOKEN | KVPAIR_TOKEN));

    return 0;

push_err:
    free(token.contents);
    return -1;
}

static command_t command_new(void)
{
    return (command_t){
        .env = vec_new(sizeof(string_t *)),
        .args = vec_new(sizeof(string_t *)),
    };
}

static void commandline_drop(commandline_t *cmdline)
{
    if(is_some(cmdline)) {
        vec_drop(&cmdline->conditionals, (FreeFn) conditional_drop);
    }
}

static void command_drop(command_t *cmd)
{
    if(is_some(cmd)) {
        vec_drop(&cmd->env, (FreeFn) free);
        vec_drop(&cmd->args, (FreeFn) free);
    }
}
