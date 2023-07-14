#include "cmdline.h"
#include "parser.h"
#include "string.h"
#include "vec.h"
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define PIPELINE_OK 0
#define PIPELINE_ERR 1
#define PIPE_R_END 0 /* Read  end */
#define PIPE_W_END 1 /* Write end */
#define openf(file, append)                                                         \
    open((file), O_CREAT, (append) ? O_APPEND : O_TRUNC, O_WRONLY)

static int conditional_execute(conditional_t *cond);
static int pipeline_execute(pipeline_t *pipeline, bool bg);
static int command_execute(command_t *command, bool piped);

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

void commandline_execute(commandline_t *cmdline, int *status)
{
    vec_t *cnds = cmdline->conditionals;
    size_t cndn = vec_len(cnds);

    for(size_t i = 0; i < cndn; i++) {
        *status = conditional_execute(vec_index(cnds, i));
    }
}

static int conditional_execute(conditional_t *cond)
{
    vec_t *pips = cond->pipelines;
    size_t pipn = vec_len(pips);
    pipeline_t *pipeline = NULL;
    int ctn = PIPELINE_OK;

    for(size_t i = 0; i < pipn; i++) {
        pipeline = vec_index(pips, i);
        pipeline_execute(pipeline, cond->is_background);

        if(ctn == PIPELINE_ERR && IS_AND(pipeline->connection)) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

static int pipeline_execute(pipeline_t *pipeline, bool bg)
{
    vec_t *commands = pipeline->commands;
    size_t cmdn = vec_len(commands);
    size_t cproc, temp;
    int ctl, status;
    int pipefd[2];
    pid_t childPID;
    pid_t childPIDs[cmdn];

    memset(childPIDs, 0, cmdn * sizeof(pid_t));

    pipe(pipefd);

    pid_t *cPIDp = childPIDs;
    for(size_t i = cproc = temp = 0; i < cmdn; i++) {
        if((childPID = command_execute(vec_index(commands, i), -1)) != FAILURE) {
            cproc++;
            *cPIDp++ = childPID;
        }
    }

    status = PIPELINE_OK;

    if(bg && IS_NONE(pipeline->connection)) {
        return status;
    } else {
        for(size_t i = 0; cproc--; i++) {
            if(waitpid(childPIDs[i], &ctl, 0) == -1) {
                WAIT_ERR(childPID);
                perror("waitpid");
            } else if(!WIFEXITED(ctl) && cproc == 0) {
                status = PIPELINE_ERR;
            }
        }
    }

    return status;
}

static int command_execute(command_t *command, bool piped)
{
    pid_t childPID = fork();

    if(childPID == -1) {
        FORK_ERR;
        return FAILURE;
    } else if(childPID == 0) {
        vec_t *args = command->args;
        size_t argc = vec_len(args);

        byte *argv[argc + 1];
        byte **argvp = argv;

        /// Pruned 'argv' with only command arguments
        byte *pruned_argv[argc + 1];
        byte **pargvp = pruned_argv;

        /// Set default file descriptors
        int outfd = STDOUT_FILENO;
        int errfd = STDERR_FILENO;
        int infd = STDIN_FILENO;

        vec_t *envs = command->env;
        size_t child_envc = vec_len(envs);
        size_t envc = 0;
        byte **environp = environ;
        size_t i;

        /// Get parent env var count
        while(is_some(*environp++)) envc++;

        /// Holds all of our env vars
        byte *envp[envc + child_envc + 1];
        byte **envptr = envp;

        /// Populate the envp array
        while(is_some(*environp)) *envptr++ = *environp++;
        for(i = 0; child_envc--; i++)
            *envptr++ = string_slice((string_t *) vec_index(envs, i), 0);
        envp[envc + child_envc] = NULL;

        /// Populate the argv array
        for(i = 0; i < argc; i++) *argvp++ = string_slice(vec_index(args, i), 0);
        argv[argc] = pruned_argv[argc] = NULL;

        bool append = false;
        for(argvp = argv; is_some(*argvp); argvp++) {
            switch(**argvp) {
                case '>':
                    if(*(*argvp + 1) == '>') {
                        append = true;
                    }
                    if(outfd != STDOUT_FILENO && close(outfd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        return FAILURE;
                    }
                    outfd = openf(*(++argvp), append);
                    if(outfd == -1) {
                        FILE_OPEN_ERR(*argvp);
                        perror("open");
                        return FAILURE;
                    }
                    append = false;
                    break;
                case '2':
                    if(*(*argvp + 2) == '>') {
                        append = true;
                    }
                    if(errfd != STDERR_FILENO && close(errfd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        return FAILURE;
                    }
                    errfd = openf(*(++argvp), append);
                    if(errfd == -1) {
                        FILE_OPEN_ERR(*argvp);
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
                    infd = open(*(++argvp), O_RDONLY);
                    if(infd == -1) {
                        FILE_OPEN_ERR(*argvp);
                        perror("open");
                        return FAILURE;
                    }
                default:
                    *pargvp++ = *argvp;
                    break;
            }
        }
        *pargvp = NULL;

        /// Redirect output, input and err streams
        if(dup2(outfd, STDOUT_FILENO) == -1 || dup2(infd, STDIN_FILENO) == -1
           || dup2(errfd, STDOUT_FILENO) == -1)
        {
            REDIRECT_ERR;
            perror("dup2");
            return FAILURE;
        }

        /// Finally execute the command
        if(execve(pruned_argv[0], pruned_argv, envp) == -1) {
            EXEC_ERR(pruned_argv[0]);
            perror("execve");
            return FAILURE;
        }
    }
    return childPID;
}
