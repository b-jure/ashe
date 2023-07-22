#include "ashe_string.h"
#include "ashe_utils.h"
#include "cmdline.h"
#include "job.h"
#include "parser.h"
#include "vec.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>

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

/// Generic too many or too few arguments warning for program 'prog'
#define PW_TOO_MANY(prog) pwarn("too many arguments provided for command '%s'", prog)
#define PW_TOO_FEW(prog) pwarn("missing argument/s for command '%s'", prog);

/// Wrapper around 'open'
#define openf(file, append)                                                              \
    open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)

/// Built-in commands
byte *builtin[] = {"exit", "pwd", "clear", "cd", "penv", "senv", "renv", "builtin"};
#define BUILTINN sizeof(builtin) / sizeof(builtin[0])

typedef struct {
    int pipefd[2];
    int closefd;
} context_t;

/// Internal functions
static context_t context_new(void);
static int conditional_execute(conditional_t *cond);
static int pipeline_execute(pipeline_t *pipeline, bool bg);
static int run_cmd(command_t *cmd, context_t *ctx, pid_t gpid, bool bg);
static int envcmd(byte *const *argv, int option);
static void penviron(void);
static void pbuiltin(void);
static void command_get_argv(command_t *cmd, byte *out[], size_t len);
static void command_get_env(command_t *cmd, byte *out[], size_t len);
static int close_pipe(int *pp);
static int add_envs_to_environ(byte *const *envp);
static int rm_envs_from_environ(byte *const *envp);
static void reset_signal_handling(void);
static void configure_pipes(size_t i, int *pipes, size_t cmdn, context_t *ctx);
static void exec_builtin(const byte *command, byte *const *argv);
static void resolve_redirections(byte *const *argvp, context_t *ctx);

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

static void reset_signal_handling(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
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
    size_t i;
    vec_t *argv = cmd->argv;
    for(i = 0; i < len; i++) out[i] = string_slice(vec_index(argv, i), 0);
    out[i] = NULL;
}

static void command_get_env(command_t *cmd, byte *out[], size_t len)
{
    size_t i;
    vec_t *env = cmd->env;
    for(i = 0; i < len; i++) out[i] = string_slice(vec_index(env, i), 0);
    out[i] = NULL;
}

static byte *format_argv(byte *const *argv)
{
    byte commandline[ARG_MAX];
    commandline[0] = NULL_TERM;
    while(is_some(*argv)) {
        strcat(commandline, *argv++);
        if(is_some(*argv))
            strcat(commandline, " ");
    }
    return strdup(commandline);
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

static void configure_pipes(size_t i, int *pipes, size_t cmdn, context_t *ctx)
{
    if(first(i)) {
        if(pipe(pipes) < 0) {
            perr();
            exit(EXIT_FAILURE);
        }
        ctx->pipefd[PIPE_W] = pipes[PIPE_W];
        ctx->closefd = pipes[PIPE_R];
    } else if(not_first(i) && not_last(i, cmdn)) {
        if(pipe(pipe_at(CURR(i), pipes)) < 0) {
            perr();
            exit(EXIT_FAILURE);
        }
        ctx->pipefd[PIPE_R] = *pipe_at(PREV(i), pipes);
        ctx->pipefd[PIPE_W] = *(pipe_at(CURR(i), pipes) + PIPE_W);
        ctx->closefd = *(pipe_at(PREV(i), pipes) + PIPE_W);
    } else {
        ctx->pipefd[PIPE_R] = *pipe_at(PREV(i), pipes);
        ctx->closefd = *(pipe_at(PREV(i), pipes) + PIPE_W);
    }
}

static void fargvs(vec_t *argv, byte **out)
{
    size_t len = vec_len(argv);
    byte commandline[ARG_MAX];
    commandline[0] = NULL_TERM;

    for(size_t i = 0; i < len; i++) {
        strcat(commandline, vec_index(argv, i));
        if(i + 1 != len)
            strcat(commandline, " ");
    }

    *out = strdup(commandline);

    if(is_null(*out)) {
        pwarn("failed to copy over the user input: %s", commandline);
        perr();
    }
}

static int pipeline_execute(pipeline_t *pipeline, bool bg)
{
    context_t ctx; /* Context, instructs forked proc which pipe stream to dup and close */
    vec_t *commands = pipeline->commands; /* All of the commands for this pipeline */
    size_t cmdn = vec_len(commands);      /* Amount of commands in this pipeline */
    int status;                           /* Return status of this pipeline */
    pid_t spgid = getpid();               /* shell process group ID */
    pid_t cpid;                           /* child Process ID */

    job_t job = job_new(pipeline->connection, bg); /* New job for this pipeline */
    if(is_null(job.processes))
        return FAILURE;

    unsigned pn = ((cmdn - 1) * 2); /* Size of pipe array */
    int pipes[pn];                  /* Pipe storage */

    /// Run the entire pipeline
    for(int i = 0; i < cmdn; i++) {
        ctx = context_new();
        /// If we have more than 1 command in the pipeline
        /// they must be connected with pipes, create and configure their pipes
        if(cmdn > 1)
            configure_pipes(i, pipes, cmdn, &ctx);

        /// Fetch the next command and run it
        command_t *cmd = vec_index(commands, i);
        if((cpid = run_cmd(cmd, &ctx, job.pgid, bg)) != FAILURE) {
            if(job.pgid == 0)
                job.pgid = cpid;
            /// Create a new process with the pid 'cpid'
            process_t temp_proc = process_new(cpid);
            /// Format the argv array into 'process.comandline' cstring and
            /// insert the process into the current job
            fargvs(cmd->argv, &temp_proc.commandline);
            /// If out of memory exit
            if(is_null(temp_proc.commandline) || !job_add_process(&job, &temp_proc)) {
                joblist_cleanup();
                exit(EXIT_FAILURE);
            }
        } else {
            /// If fork failed just exit
            joblist_cleanup();
            exit(EXIT_FAILURE);
        }

        /// Close the last pipe if it fails exit
        if(not_first(i) && close_pipe(pipe_at(PREV(i), pipes)) < 0) {
            joblist_cleanup();
            exit(EXIT_FAILURE);
        }
    }

    if(job.foreground)
        /// Move process into foreground and wait for its children
        job_move_to_fg(&job, false);
    else {
        job_print_launch(&job, joblist_len());
        /// Otherwise add the job to joblist and return
        if(!vec_push(joblist.jobs, &job)) {
            joblist_cleanup();
            exit(EXIT_FAILURE);
        }
        /// This is no-op
        job_move_to_bg(&job, false);
    }

    return status;
}

static int run_cmd(command_t *cmd, context_t *ctx, pid_t gpid, bool bg)
{
    pid_t childPID = fork();

    if(childPID == -1) {
        pwarn("failed forking parent process [ID:%d]", getpid());
        perr();
        return FAILURE;
    } else if(childPID == 0) {
        /// Extract command and its arguments
        size_t argc = vec_len(cmd->argv);
        byte *argv[argc + 1];
        command_get_argv(cmd, argv, argc);
        /// Extract environment variables
        size_t envc = vec_len(cmd->env);
        byte *env[envc + 1];
        command_get_env(cmd, env, envc);
        /// Add environment variables to environ
        if(add_envs_to_environ(env) < 0)
            exit(EXIT_FAILURE);

        pid_t pid = getpid(); /* Current process ID */

        /// If this is the first command set the gpid as pid
        /// and give it the terminal
        if(gpid == 0) {
            gpid = pid;
            if(!bg && tcsetpgrp(TERMINAL_FD, gpid) < 0) {
                pwarn("failed giving terminal to process group [ID:%d]", gpid);
                perr();
                exit(EXIT_FAILURE);
            }
        }
        /// Move this process into the new process group
        if(setpgid(pid, gpid) < 0) {
            pwarn(
                "process [ID:%d] failed setting itself into process group [ID:%d]",
                pid,
                gpid);
            perr();
            exit(EXIT_FAILURE);
        }

        /// Reset signal handling to default
        reset_signal_handling();
        /// Parse and configure all the redirections
        resolve_redirections(argv, ctx);
        /// Try execute builtin command
        exec_builtin(argv[0], argv);
        /// Execute the non-builtin command
        if(execvp(argv[0], argv) < 0) {
            pwarn("failed to run the command '%s'", argv[0]);
            perr();
            exit(EXIT_FAILURE);
        }
    }

    return childPID;
}

static void resolve_redirections(byte *const *argvp, context_t *ctx)
{
    /// Configure pipe streams and close unnecessary ones
    if(dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0
       || dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0)
    {
        perr();
        exit(EXIT_FAILURE);
    }
    /// Close the redundant pipe end, if any...
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
    const byte **pargvp = (const byte **) argvp;

    for(; is_some(*argvp); argvp++) {
        switch(**argvp) {
            case '>':
                if(*(*argvp + 1) == '>')
                    append = true;

                if((redfd = openf(*(++argvp), append)) < 0) {
                    pwarn("failed to open a file '%s'", *argvp);
                    perr();
                    exit(EXIT_FAILURE);
                } else if(close(outfd) == -1) {
                    pwarn("failed to close fd '%d'", outfd);
                    perr();
                    exit(EXIT_FAILURE);
                } else
                    outfd = redfd;
                break;
            case '2':
                if(*(*argvp + 2) == '>')
                    append = true;

                if((redfd = openf(*(++argvp), append)) < 0) {
                    pwarn("failed to open a file '%s'", *argvp);
                    perr();
                    exit(EXIT_FAILURE);
                } else if(close(errfd) == -1) {
                    pwarn("failed to close fd '%d'", errfd);
                    perr();
                    exit(EXIT_FAILURE);
                } else
                    errfd = redfd;
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
                } else
                    infd = redfd;
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
}

static void exec_builtin(const byte *command, byte *const *argv)
{
    if(strcmp(command, "exit") == 0) {
        exit(EXIT_SUCCESS);
    } else if(strcmp(command, "cd") == 0) {
        if(is_null(argv[1])) {
            if(chdir(getenv(HOME)) == -1) {
                perr();
                exit(EXIT_FAILURE);
            }
            exit(SUCCESS);
        } else if(is_some(argv[2])) {
            PW_TOO_MANY(argv[0]);
            pusage("cd [DIRNAME]");
            exit(EXIT_FAILURE);
        } else if(chdir(argv[1]) < 0) {
            perr();
            exit(EXIT_FAILURE);
        } else {
            exit(EXIT_SUCCESS);
        }
    } else if(strcmp(command, "penv") == 0) {
        if(is_null(argv[1])) {
            exit(envcmd(NULL, P_ALL));
        } else if(is_some(argv[1]) && is_null(argv[2])) {
            exit(envcmd(argv, P_ENV));
        } else {
            PW_TOO_MANY(argv[0]);
            pusage("penv [VARNAME]");
            exit(EXIT_FAILURE);
        }
    } else if(strcmp(command, "senv") == 0) {
        if(is_null(argv[1]) || is_null(argv[2])) {
            PW_TOO_FEW(argv[0]);
            pusage("senv [VARNAME] [VARVALUE]");
            exit(EXIT_FAILURE);
        } else if(is_some(argv[3])) {
            PW_TOO_MANY(argv[0]);
            pusage("senv [VARNAME] [VARVALUE]");
            exit(EXIT_FAILURE);
        } else {
            exit(envcmd(argv, SET_ENV));
        }
    } else if(strcmp(command, "renv") == 0) {
        if(is_null(argv[1])) {
            PW_TOO_FEW(argv[0]);
            pusage("renv [VARNAME] [VARVALUE]");
            exit(EXIT_FAILURE);
        } else if(is_some(argv[2])) {
            PW_TOO_MANY(argv[0]);
            pusage("renv [VARNAME] [VARVALUE]");
            exit(EXIT_FAILURE);
        } else {
            exit(envcmd(argv, RM_ENV));
        }
    } else if(strcmp(command, "pwd") == 0) {
        byte buff[PATH_MAX];
        if(is_null(getcwd(buff, PATH_MAX))) {
            perr();
            exit(EXIT_FAILURE);
        } else {
            printf("%s\n", buff);
            exit(EXIT_SUCCESS);
        }
    } else if(strcmp(command, "clear") == 0) {
        exit(system("clear"));
    } else if(strcmp(command, "builtin") == 0) {
        if(is_some(argv[1])) {
            PW_TOO_MANY(argv[0]);
            pusage("builtin");
            exit(EXIT_FAILURE);
        } else {
            pbuiltin();
            exit(EXIT_SUCCESS);
        }
    }
}
