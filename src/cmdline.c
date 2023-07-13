#include "cmdline.h"
#include "parser.h"
#include "string.h"
#include "vec.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int conditional_execute(conditional_t *cond);
static int pipeline_execute(pipeline_t *pipeline);
static byte **command_as_argv(command_t *command);

commandline_t commandline_new(void)
{
    return (commandline_t){
        .conditionals = vec_new(sizeof(conditional_t)),
    };
}

void commandline_drop(commandline_t *cmdline)
{
    if(is_some(cmdline)) {
        vec_drop(&cmdline->conditionals, (FreeFn) conditional_drop);
    }
}

int commandline_execute(commandline_t *cmdline)
{
#ifdef ASHE_DEBUG
    assert(is_some(cmdline));
#endif
    vec_t *conditionals = cmdline->conditionals;
    size_t size = vec_len(conditionals);

    for(size_t i = 0; i < size; i++) {
        conditional_t *cond = vec_index(conditionals, i);
        if(conditional_execute(cond) == FAILURE) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

static int conditional_execute(conditional_t *cond)
{
#ifdef ASHE_DEBUG
    assert(is_some(cond));
#endif
    vec_t *pipelines = cond->pipelines;
    size_t size = vec_len(pipelines);

    for(size_t i = 0; i < size; i++) {
        pipeline_t *pipeline = vec_index(pipelines, i);
        if(pipeline_execute(pipeline) == FAILURE) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

static int pipeline_execute(pipeline_t *pipeline)
{
#ifdef ASHE_DEBUG
    assert(is_some(pipeline));
#endif
    vec_t *commands = pipeline->commands;
    size_t size = vec_len(commands);
    byte **args;

    for(size_t i = 0; i < size; i++) {
        command_t *command = vec_index(commands, i);
        args = command_as_argv(command);
        if(i + 1 != size) {
            //todo
        }
    }

    return SUCCESS;
}

static byte **command_as_argv(command_t *command)
{
#ifdef ASHE_DEBUG
    assert(is_some(command));
#endif
    vec_t *args = command->args;
    size_t len = vec_len(args);
    vec_t *argv = vec_with_capacity(sizeof(byte *), len + 1);
    byte *null = NULL;
    byte **inner;
    byte *arg;

    for(size_t i = 0; i < len; i++) {
        arg = string_slice((string_t *) vec_index(args, i), 0);
        if(!vec_push(argv, &arg)) {
            vec_drop(&argv, NULL);
            return NULL;
        }
    }

    if(!vec_push(argv, &null)) {
        vec_drop(&argv, NULL);
        return NULL;
    }

    inner = vec_inner_unsafe(argv);
    free(argv);
    return inner;
}
