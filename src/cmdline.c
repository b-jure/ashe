#include "cmdline.h"
#include "parser.h"
#include "string.h"
#include "vec.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define CHILD_PROCESS 0
#define openf(file, append)                                                         \
    open((file), O_CREAT, (append) ? O_APPEND : O_TRUNC, O_WRONLY)

static int conditional_execute(conditional_t *cond);
static int pipeline_execute(pipeline_t *pipeline);
static int command_execute(command_t *command, int pipefd);
static void **command_as_argv(command_t *command, byte **out);
static void **command_as_envp(command_t *command, byte **out);
static void **command_as_internal(command_t *command, vec_t *args, byte **out);

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
    int pipefd = -1;

    for(size_t i = 0; i < size; i++) {
        command_t *command = vec_index(commands, i);
        command_execute(command, -1);
    }

    return SUCCESS;
}

/// TODO: make command_t contain input, output, err stream
/// filenames that get pushed into argv during parsing, this
/// would require creating a seperate token for each kind of
/// redirection or the other way would be to check the string_t
/// of each token and figure out that way which one is out, err and input
/// redirection. This will result in cleaner code during command
/// execution by skipping over the pruning of 'argv' and also
/// performance increase where we won't need to open multiple files
/// in case mutliple redirections of the same stream to multiple files.
static int command_execute(command_t *command, int pipefd)
{
#ifdef ASHE_DEBUG
    assert(is_some(command));
#endif
    /// Fork immediately in case command arguments contain
    /// redirection, this way our file descriptors won't get
    /// messed with and we won't need to open/close files.
    int pid = fork();

    if(pid == -1) {
        FORK_ERR;
        return FAILURE;
    } else if(pid == CHILD_PROCESS) {

        /// Extract args and env variables
        size_t argc = vec_len(command->args);
        size_t envc = vec_len(command->env);
        byte *argv[argc + 1];
        byte *envp[envc + 1];
        byte **ptr = argv; /* Loop pointer for traversal */

        /// Pruned 'argv' with only command arguments
        byte *pruned_argv[argc + 1];
        byte **cptr = pruned_argv; /* Loop pointer for traversal */

        /// Set default file descriptors
        int outfd = (pipefd != -1) ? pipefd : STDOUT_FILENO;
        int errfd = STDERR_FILENO;
        int infd = STDIN_FILENO;

        /// Null out the arrays for 'execve' call
        argv[argc] = NULL;
        envp[envc] = NULL;
        pruned_argv[argc] = NULL;
        /// In case we have redirections make sure
        /// we are correctly appending or truncating
        bool append = false;

        /// Configure the file descriptors and prune the argv into 'clean_argv'
        /// TODO: remove all of this by having command_t hold filenames of all
        /// streams (or NULL in case of no redirections).
        for(; is_some(*ptr); ptr++) {
            switch(**ptr) {
                case '>':
                    if(*(*ptr + 1) == '>') {
                        append = true;
                    }
                    if(outfd != STDOUT_FILENO && close(outfd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        return FAILURE;
                    }
                    outfd = openf(*(++ptr), append);
                    if(outfd == -1) {
                        FILE_OPEN_ERR(*ptr);
                        perror("open");
                        return FAILURE;
                    }
                    append = false;
                    break;
                case '2':
                    if(*(*ptr + 2) == '>') {
                        append = true;
                    }
                    if(errfd != STDERR_FILENO && close(errfd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        return FAILURE;
                    }
                    errfd = openf(*(++ptr), append);
                    if(errfd == -1) {
                        FILE_OPEN_ERR(*ptr);
                        perror("open");
                        return FAILURE;
                    }
                    append = false;
                    break;
                case '<':
                    if(infd != STDIN_FILENO && close(infd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        return FAILURE;
                    }
                    infd = open(*(++ptr), O_RDONLY);
                    if(infd == -1) {
                        FILE_OPEN_ERR(*ptr);
                        perror("open");
                        return FAILURE;
                    }
                default:
                    *cptr++ = *ptr;
                    break;
            }
        }
        /// Null out pruned argv for 'execve' call
        *cptr = NULL;

        /// Re-route output, input and err streams
        if(outfd != STDOUT_FILENO) {
            if(dup2(outfd, STDOUT_FILENO) == -1) {
                OUTPUT_REDIRECT_ERR;
                perror("dup2");
                return FAILURE;
            }
        }
        if(infd != STDIN_FILENO) {
            if(dup2(infd, STDIN_FILENO) == -1) {
                INPUT_REDIRECT_ERR;
                perror("dup2");
                return FAILURE;
            }
        }
        if(errfd != STDERR_FILENO) {
            if(dup2(errfd, STDOUT_FILENO) == -1) {
                OUTPUT_REDIRECT_ERR;
                perror("dup2");
                return FAILURE;
            }
        }

        if(execve(pruned_argv[0], pruned_argv, envp) == -1) {
            EXEC_ERR(pruned_argv[0]);
            perror("execve");
            return FAILURE;
        }
#ifdef ASHE_DEBUG
        printf("UNREACHABLE CODE\n");
        assert(false);
#endif
    }

    return SUCCESS;
}

static void **command_as_argv(command_t *command, byte **out)
{
    return command_as_internal(command, command->args, out);
}

static void **command_as_envp(command_t *command, byte **out)
{
    return command_as_internal(command, command->env, out);
}

static void **command_as_internal(command_t *command, vec_t *args, byte **out)
{
#ifdef ASHE_DEBUG
    assert(is_some(command));
#endif
    size_t argc = vec_len(args);
    byte *arg;
    for(size_t i = 0; i < argc; i++, out++) {
        arg = string_slice((string_t *) vec_index(args, i), 0);
        *out = arg;
    }
}
