#include "ashe_utils.h"
#define _DEFAULT_SOURCE
#include "ashe_string.h"
#include "cmdline.h"
#include "parser.h"
#include "vec.h"
#include <assert.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define pipe_at(i) (i * 2)
#define not_last(i, total) (i + 1 != total)
#define not_first(i) (i != 0)
#define PIPE_R 0 /* Read  end */
#define PIPE_W 1 /* Write end */
#define openf(file, append)                                                         \
    open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)

typedef struct {
    int pipefd[2];
} context_t;

static int conditional_execute(conditional_t *cond);
static int pipeline_execute(pipeline_t *pipeline, bool bg);
static int command_execute(command_t *command, context_t *ctx, int *pipes);

context_t context_new(void)
{
    return (context_t){
        .pipefd = {STDIN_FILENO, STDOUT_FILENO},
    };
}

commandline_t commandline_new(void)
{
    return (commandline_t){
        .conditionals = vec_new(sizeof(conditional_t)),
    };
}

void commandline_clear(commandline_t *cmdline)
{
    if(is_some(cmdline)) {
        printf("Clearing commandline...\n");
        vec_clear(cmdline->conditionals, (FreeFn) conditional_drop);
        printf("done! [commandline clear]\n");
    }
}

void commandline_drop(commandline_t *cmdline)
{
    if(is_some(cmdline)) {
        printf("Dropping commandline\n");
        vec_clear(cmdline->conditionals, (FreeFn) conditional_drop);
    }
}

void commandline_execute(commandline_t *cmdline, int *status)
{
    vec_t *cnds = cmdline->conditionals;
    size_t cndn = vec_len(cnds);

    for(size_t i = 0; i < cndn; i++) {
        printf("Executing conditional [%ld]\n", i);
        *status = conditional_execute(vec_index(cnds, i));
    }
    printf(
        "Returning from commandline with status '%s'\n",
        (*status) ? "FAILURE" : "SUCCESS");
}

static int conditional_execute(conditional_t *cond)
{
    vec_t *pips = cond->pipelines;
    size_t pipn = vec_len(pips);
    pipeline_t *pipeline = NULL;
    int ctn = SUCCESS;

    for(size_t i = 0; i < pipn; i++) {
        pipeline = vec_index(pips, i);
        printf("Executing pipeline [%ld]\n", i);
        ctn = pipeline_execute(pipeline, cond->is_background);

        if(ctn == FAILURE && IS_AND(pipeline->connection)) {
            return FAILURE;
        } else if(ctn == SUCCESS && IS_OR(pipeline->connection)) {
            return SUCCESS;
        }
    }

    printf(
        "Returning from conditional with status '%s'\n",
        (ctn) ? "FAILURE" : "SUCCESS");
    return ctn;
}

static int pipeline_execute(pipeline_t *pipeline, bool bg)
{
    context_t ctx; /* Context, instructs child proc which stream to dup */
    vec_t *commands = pipeline->commands; /* All of the commands for this pipeline */
    size_t cmd_n = vec_len(commands);     /* Amount of commands in this pipeline */
    size_t cproc; /* Number of successfully forked proccesses to wait for */
    size_t i;
    int ctl;            /* Control flag storage for waitpid */
    int status;         /* Return status of this pipeline */
    pid_t cPID;         /* child Process ID */
    pid_t cPIDs[cmd_n]; /* All successfully forked child Process ID's */
    pid_t *cPIDp = cPIDs;

    assert(cmd_n > 0);
    unsigned pn = ((cmd_n - 1) * 2); /* Size of pipe array */
    int pipes[pn + 1];               /* Pipe storage */
    pipes[pn] = -1;                  /* Set end */

    for(i = 0; i < (cmd_n - 1); i++) {
        if(pipe(pipes + (i * 2)) < 0) {
            PIPE_OPEN_ERR;
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for(i = cproc = 0; i < cmd_n; i++) {
        ctx = context_new();

        if(cmd_n > 1) {
            if(!not_first(i)) {
                ctx.pipefd[PIPE_W] = pipes[pipe_at(i) + 1]; /* This PIPE_W */
            } else if(not_first(i) && not_last(i, cmd_n)) {
                ctx.pipefd[PIPE_R] = pipes[pipe_at(i) - 2]; /* Last PIPE_R */
                ctx.pipefd[PIPE_W] = pipes[pipe_at(i) + 1]; /* This PIPE_W */
            } else {
                ctx.pipefd[PIPE_R] = pipes[pipe_at(i) - 2]; /* Last PIPE_R */
            }
        }

        if((cPID = command_execute(vec_index(commands, i), &ctx, pipes)) != FAILURE)
        {
            cproc++;
            *cPIDp++ = cPID;
        } else {
            break; /* Stop executing */
        }
    }

    for(i = 0; i < pn; i++) {
        if(close(pipes[i]) < 0) {
            FILE_CLOSE_ERR;
            perror("close");
            exit(EXIT_FAILURE);
        }
    }

    status = (cPID == FAILURE) ? FAILURE : SUCCESS;
    if(bg && IS_NONE(pipeline->connection)) {
        /// This is last or only pipeline that is ran
        /// in background don't wait for child processes
        return status;
    } else {
        for(i = 0; cproc--; i++) {
        try_again:
            if(waitpid(cPIDs[i], &ctl, 0) == -1) {
                WAIT_ERR(cPIDs[i]);
                perror("waitpid");
                if(sleep(1) != 0) exit(EXIT_FAILURE);
                goto try_again;
            } else if(
                status != FAILURE && cproc == 0
                && (!WIFEXITED(ctl) || WEXITSTATUS(ctl) != 0))
            {
                status = FAILURE;
            }
        }
    }

    printf("Returning with status '%s'\n", (status) ? "FAILURE" : "SUCCESS");
    return status;
}

static int command_execute(command_t *command, context_t *ctx, int *pipes)
{
    pid_t childPID = fork();

    if(childPID == -1) {
        FORK_ERR;
        perror("fork");
        return FAILURE; /* For now recoverable error */
    } else if(childPID == 0) {
        /// COMMAND ARGS ///
        vec_t *args = command->args; /* cstrings (command and arguments) */
        size_t argc = vec_len(args); /* Number of arguments including command */
        byte *argv[argc + 1]; /* Storage for command arguments with redirections */
        byte **argvp = argv;  /* Pointer for looping */
        byte **pargvp = argv; /* Pointer for pruning argv array */

        /// ENV VARS ///
        vec_t *envs = command->env;  /* Env variables */
        size_t envc = vec_len(envs); /* Number of env variables */
        byte *var;                   /* Temp storage for env var */

        /// PIPE STREAMS ///
        if(dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0
           || dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0)
        {
            REDIRECT_ERR;
            perror("dup2");
            exit(EXIT_FAILURE);
        }

        /// DEFAULT STREAMS ///
        int infd = PIPE_R;         /* Default input stream */
        int outfd = PIPE_W;        /* Default output stream */
        int errfd = STDERR_FILENO; /* Default err stream */
        int redfd = -1;            /* Redirections fd */

        /// Add env vars
        while(envc--) {
            if(putenv((var = string_slice(vec_index(envs, envc), 0))) == -1) {
                FAILED_SETTING_ENVVAR(var);
                perror("putenv");
                exit(EXIT_FAILURE);
            }
        }

        /// Populate and null out the argv array
        for(size_t i = 0; i < argc; i++)
            *argvp++ = string_slice(vec_index(args, i), 0);
        argv[argc] = NULL;

        /// Parse redirections and also prune out argv leaving
        /// only command and its arguments
        bool append = false;
        for(argvp = argv; is_some(*argvp); argvp++) {
            switch(**argvp) {
                case '>':
                    if(*(*argvp + 1) == '>') {
                        append = true;
                    }

                    if((redfd = openf(*(++argvp), append)) == -1) {
                        FILE_OPEN_ERR(*argvp);
                        perror("open");
                        exit(EXIT_FAILURE);
                    } else if(close(outfd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        exit(EXIT_FAILURE);
                    } else {
                        outfd = redfd;
                    }

                    break;
                case '2':
                    if(*(*argvp + 2) == '>') {
                        append = true;
                    }

                    if((redfd = openf(*(++argvp), append)) == -1) {
                        FILE_OPEN_ERR(*argvp);
                        perror("open");
                        exit(EXIT_FAILURE);
                    } else if(close(errfd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        exit(EXIT_FAILURE);
                    } else {
                        errfd = redfd;
                    }

                    break;
                case '<':
                    if((redfd = open(*(++argvp), O_RDONLY)) == -1) {
                        FILE_OPEN_ERR(*argvp);
                        perror("open");
                        exit(EXIT_FAILURE);
                    } else if(close(infd) == -1) {
                        FILE_CLOSE_ERR;
                        perror("close");
                        exit(EXIT_FAILURE);
                    } else {
                        infd = redfd;
                    }

                    break;
                default:
                    *pargvp++ = *argvp;
                    break;
            }
            append = false;
        }
        *pargvp = NULL; /* Null out the pruned argv */

        /// Exec the command
        if(execvp(argv[0], argv) == -1) {
            EXEC_ERR(argv[0]);
            perror("execve");
            exit(EXIT_FAILURE);
        }
    }

    return childPID;
}
