#define _DEFAULT_SOURCE
#include "ashe_string.h"
#include "ashe_utils.h"
#include "cmdline.h"
#include "parser.h"
#include "vec.h"

#include <assert.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define PIPE_R 0                            /* Read  end of a pipe */
#define PIPE_W 1                            /* Write end of a pipe */
#define PREV(i) (i - 1)                     /* Prev pipe at index i */
#define CURR(i) (i)                         /* Current pipe at index i */
#define pipe_at(i, pipes) (pipes + (i * 2)) /* Ptr PIPE_R to pipe in pipes at index i */
#define not_last(i, total) (i + 1 != total) /* Command not last in pipeline */
#define not_first(i) (i != 0)               /* Command not first in pipeline */
#define first(i) (i == 0)                   /* Command is first in the pipeline */

/// Options for envcmd function
#define ADD_ENV 0 /* Add env var name/value */
#define SET_ENV 1 /* Add/overwrite env var name/value */
#define RM_ENV 2  /* Remove env var */
#define P_ENV 3   /* Print env var */
#define P_ALL 4   /* Print all environ */

#define NO_RUN 1 /* ret value if command_execute_no_fork didn't execute the cmd */

/// Generic too many or too few arguments warning for program 'prog'
#define PW_TOO_MANY(prog) pwarn("too many arguments provided for command '%s'", prog)
#define PW_TOO_FEW(prog) pwarn("missing argument/s for command '%s'", prog);

/// Wrapper around 'open'
#define openf(file, append)                                                              \
    open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)

/// Built-in commands
byte *builtin[] = {"exit", "pwd", "clear", "cd", "penv", "senv", "renv", "builtin"};
#define BUILTINN sizeof(builtin) / sizeof(builtin[0])

/// Stores context required for each proccess/built-in command
/// to correctly dup2 the correct pipe fd and close the redundant one.
typedef struct {
    int pipefd[2];
    int closefd;
} context_t;

/// Holds the env vars for this process
extern byte **environ;

/// Internal functions
static context_t context_new(void);
static int conditional_execute(conditional_t *cond);
static int pipeline_execute(pipeline_t *pipeline, bool bg);
static int run_forked(byte *const *argv, context_t *ctx);
static int run_builtin(byte *const *argv, context_t *ctx);
static int envcmd(byte *const *argv, int option);
static void penviron(void);
static void pbuiltin(void);
static void command_get_argv(command_t *cmd, byte *out[], size_t len);
static void command_get_env(command_t *cmd, byte *out[], size_t len);
static int close_pipe(int *pp);
static int add_envs_to_environ(byte *const *envp);
static int rm_envs_from_environ(byte *const *envp);

static context_t context_new(void)
{
    return (context_t){
        .pipefd = {STDIN_FILENO, STDOUT_FILENO},
        .closefd = -1,
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
        vec_clear(cmdline->conditionals, (FreeFn) conditional_drop);
    }
}

void commandline_drop(commandline_t *cmdline)
{
    if(is_some(cmdline)) {
        vec_clear(cmdline->conditionals, (FreeFn) conditional_drop);
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
    int ctn = SUCCESS;

    for(size_t i = 0; i < pipn; i++) {
        pipeline = vec_index(pips, i);
        ctn = pipeline_execute(pipeline, cond->is_background);

        if(ctn == FAILURE && IS_AND(pipeline->connection)) {
            return FAILURE;
        } else if(ctn == SUCCESS && IS_OR(pipeline->connection)) {
            return SUCCESS;
        }
    }

    return ctn;
}

static int envcmd(byte *const *argv, int option)
{
    const byte *temp = NULL;
    int status = FAILURE;

    switch(option) {
        case ADD_ENV:
        case SET_ENV:
            if((status = setenv(argv[1], argv[2], (option == ADD_ENV ? 0 : 1))) < 0)
                perr();
            break;
        case RM_ENV:
            status = unsetenv(argv[1]);
            break;
        case P_ENV:
            if(is_some(temp = getenv(argv[1]))) {
                printf("%s\n", temp);
                status = SUCCESS;
            }
            break;
        case P_ALL:
            penviron();
            status = SUCCESS;
            break;
        default:
            break;
    }

    return status;
}

static void pbuiltin(void)
{
    for(size_t i = 0; i < BUILTINN; i++) printf("%s\n", builtin[i]);
}

static void penviron(void)
{
    int i = 0;
    while(is_some(environ[i])) printf("%s\n", environ[i++]);
}

static void command_get_argv(command_t *cmd, byte *out[], size_t len)
{
    vec_t *argv = cmd->argv;
    for(size_t i = 0; i < len; i++) out[i] = string_slice(vec_index(argv, i), 0);
}

static void command_get_env(command_t *cmd, byte *out[], size_t len)
{
    vec_t *env = cmd->env;
    for(size_t i = 0; i < len; i++) out[i] = string_slice(vec_index(env, i), 0);
}

static int run_builtin(byte *const *argv, context_t *ctx)
{
    int status = NO_RUN;
    byte buff[PATH_MAX];
    int oldin;
    int oldout;

    const byte *command = argv[0];

    /// Save old input and output
    if((oldin = dup(STDIN_FILENO)) < 0 || (oldout = dup(STDOUT_FILENO)) < 0) {
        perr();
        exit(EXIT_FAILURE);
    }

    /// Overwrite standard input and output with pipe
    if(dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0
       || dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0)
    {
        perr();
        exit(EXIT_FAILURE);
    }

    if(strcmp(command, "exit") == 0) {
        exit(EXIT_SUCCESS);
    } else if(strcmp(command, "cd") == 0) {
        if(is_null(argv[1])) {
            if((status = chdir(getenv(HOME))) == -1) {
                perr();
                status = FAILURE;
            }
        } else if(is_some(argv[2])) {
            PW_TOO_MANY(argv[0]);
            pusage("cd [DIRNAME]");
            status = FAILURE;
        } else if((status = chdir(argv[1])) < 0) {
            perr();
            status = FAILURE;
        }
    } else if(strcmp(command, "penv") == 0) {
        if(is_null(argv[1])) {
            status = envcmd(NULL, P_ALL);
        } else if(is_some(argv[1]) && is_null(argv[2])) {
            status = envcmd(argv, P_ENV);
        } else {
            PW_TOO_MANY(argv[0]);
            pusage("penv [VARNAME]");
            status = FAILURE;
        }
    } else if(strcmp(command, "senv") == 0) {
        if(is_null(argv[1]) || is_null(argv[2])) {
            PW_TOO_FEW(argv[0]);
            pusage("senv [VARNAME] [VARVALUE]");
            status = FAILURE;
        } else if(is_some(argv[3])) {
            PW_TOO_MANY(argv[0]);
            pusage("senv [VARNAME] [VARVALUE]");
            status = FAILURE;
        } else {
            status = envcmd(argv, SET_ENV);
        }
    } else if(strcmp(command, "renv") == 0) {
        if(is_null(argv[1])) {
            PW_TOO_FEW(argv[0]);
            pusage("renv [VARNAME] [VARVALUE]");
            status = FAILURE;
        } else if(is_some(argv[2])) {
            PW_TOO_MANY(argv[0]);
            pusage("renv [VARNAME] [VARVALUE]");
            status = FAILURE;
        } else {
            status = envcmd(argv, RM_ENV);
        }
    } else if(strcmp(command, "pwd") == 0) {
        if(is_null(getcwd(buff, PATH_MAX))) {
            perr();
            status = FAILURE;
        } else {
            printf("%s\n", buff);
            status = SUCCESS;
        }
    } else if(strcmp(command, "clear") == 0 && (status = system("clear")) != 0) {
        perr();
        status = FAILURE;
    } else if(strcmp(command, "builtin") == 0) {
        if(is_some(argv[1])) {
            PW_TOO_MANY(argv[0]);
            pusage("builtin");
            status = FAILURE;
        } else {
            pbuiltin();
            status = SUCCESS;
        }
    }

    /// Restore old input and output
    if(dup2(oldin, STDIN_FILENO) < 0 || dup2(oldout, STDOUT_FILENO) < 0) {
        perr();
        exit(EXIT_FAILURE);
    }

    return status;
}

static int close_pipe(int *pp)
{
    if(close(*pp) < 0 || close(*(pp + 1)) < 0) {
        pwarn("failed to close pipe [Wfd:%d|Rfd:%d]", *pp, *(pp + 1));
        perr();
        return FAILURE;
    }
    return SUCCESS;
}

static int add_envs_to_environ(byte *const *envp)
{
    while(is_some(*envp))
        if(putenv(*envp++) < 0) {
            pwarn("failed adding env var to environ '%s'", *(envp - 1));
            perr();
            return FAILURE;
        }

    return SUCCESS;
}

static int rm_envs_from_environ(byte *const *envp)
{
    while(is_some(*envp))
        if(unsetenv(*envp++) < 0) {
            pwarn("failed removing env var from environ '%s'", *(envp - 1));
            perr();
            return FAILURE;
        }

    return SUCCESS;
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

    for(i = cproc = 0; i < cmd_n; i++) {
        ctx = context_new();

        if(cmd_n > 1) {
            if(first(i)) {
                if(pipe(pipes) < 0) {
                    perr();
                    exit(EXIT_FAILURE);
                }
                ctx.pipefd[PIPE_W] = pipes[PIPE_W];
                ctx.closefd = pipes[PIPE_R];
            } else if(not_first(i) && not_last(i, cmd_n)) {
                if(pipe(pipe_at(CURR(i), pipes)) < 0) {
                    perr();
                    exit(EXIT_FAILURE);
                }
                ctx.pipefd[PIPE_R] = *pipe_at(PREV(i), pipes);
                ctx.pipefd[PIPE_W] = *(pipe_at(CURR(i), pipes) + PIPE_W);
                ctx.closefd = *(pipe_at(PREV(i), pipes) + PIPE_W);
            } else {
                ctx.pipefd[PIPE_R] = *pipe_at(PREV(i), pipes);
                ctx.closefd = *(pipe_at(PREV(i), pipes) + PIPE_W);
            }
        }

        int builtin;
        command_t *cmd = vec_index(commands, i);
        size_t argc = vec_len(cmd->argv);
        size_t envc = vec_len(cmd->env);
        byte *argv[argc + 1];
        byte *env[envc + 1];

        command_get_argv(cmd, argv, argc);
        argv[argc] = NULL;
        command_get_env(cmd, env, envc);
        env[envc] = NULL;

        byte *const *envp = env;
        if(add_envs_to_environ(envp) < 0) exit(EXIT_FAILURE);

        if((builtin = run_builtin(argv, &ctx)) == NO_RUN) {
            if((cPID = run_forked(argv, &ctx)) != FAILURE) {
                cproc++;
                *cPIDp++ = cPID;
                status = SUCCESS;
            } else
                status = FAILURE;
        } else
            status = builtin;

        if(rm_envs_from_environ(envp) < 0) exit(EXIT_FAILURE);

        /// Close the last pipe
        if(not_first(i) && close_pipe(pipe_at(PREV(i), pipes)) < 0) exit(EXIT_FAILURE);
    }

    if(bg && IS_NONE(pipeline->connection)) {
        return status;
    } else {
        for(i = 0; cproc--; i++) {
        try_again:
            if(waitpid(cPIDs[i], &ctl, 0) < 0) {
                perr();
                sleep(1);
                goto try_again;
            } else if(
                status != FAILURE && cproc == 0
                && (!WIFEXITED(ctl) || WEXITSTATUS(ctl) != 0))
            {
                status = FAILURE;
            }
        }
    }

    return status;
}

static int run_forked(byte *const *argv, context_t *ctx)
{
    pid_t childPID = fork();

    if(childPID == -1) {
        pwarn("failed forking parent process [PID:%d]", getpid());
        perr();
        return FAILURE;
    } else if(childPID == 0) {
        byte *const *argvp = argv;                  /* Pointer for looping cmd args */
        const byte **pargvp = (const byte **) argv; /* Pointer for pruning argv array */

        /// Configure pipe streams and close unnecessary ones
        if(dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0
           || dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0)
        {
            perr();
            exit(EXIT_FAILURE);
        }

        /// Close the redundant pipe end if any
        if(ctx->closefd != -1 && close(ctx->closefd) < 0) {
            perr();
            exit(EXIT_FAILURE);
        }

        /// Default streams for this fork
        int infd = PIPE_R;         /* Default input stream */
        int outfd = PIPE_W;        /* Default output stream */
        int errfd = STDERR_FILENO; /* Default err stream */
        int redfd = -1;            /* Redirections fd */

        /// Parse redirections and also prune out argv leaving
        /// only command and its arguments without redirections
        bool append = false;
        for(; is_some(*argvp); argvp++) {
            switch(**argvp) {
                case '>':
                    if(*(*argvp + 1) == '>') {
                        append = true;
                    }

                    if((redfd = openf(*(++argvp), append)) < 0) {
                        pwarn("failed to open a file '%s'", *argvp);
                        perr();
                        exit(EXIT_FAILURE);
                    } else if(close(outfd) == -1) {
                        pwarn("failed to close fd '%d'", outfd);
                        perr();
                        exit(EXIT_FAILURE);
                    } else {
                        outfd = redfd;
                    }

                    break;
                case '2':
                    if(*(*argvp + 2) == '>') {
                        append = true;
                    }

                    if((redfd = openf(*(++argvp), append)) < 0) {
                        pwarn("failed to open a file '%s'", *argvp);
                        perr();
                        exit(EXIT_FAILURE);
                    } else if(close(errfd) == -1) {
                        pwarn("failed to close fd '%d'", errfd);
                        perr();
                        exit(EXIT_FAILURE);
                    } else {
                        errfd = redfd;
                    }

                    break;
                case '<':
                    if((redfd = open(*(++argvp), O_RDONLY)) < 0) {
                        pwarn("failed to open a file '%s'", *argvp);
                        perr();
                        exit(EXIT_FAILURE);
                    } else if(close(infd) < 0) {
                        pwarn("failed to close fd '%d'", infd);
                        perr();
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

        /// Ensure all the redirections are taken care of
        if(dup2(infd, STDIN_FILENO) == -1 || dup2(outfd, STDOUT_FILENO) == -1
           || dup2(errfd, STDERR_FILENO) == -1)
        {
            perr();
            exit(EXIT_FAILURE);
        }

        /// Execute the program
        if(execvp(argv[0], argv) < 0) {
            pwarn("failed to run the command '%s'", argv[0]);
            perr();
            exit(EXIT_FAILURE);
        }
    }

    return childPID;
}
