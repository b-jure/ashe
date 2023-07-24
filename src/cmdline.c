#include "ashe_string.h"
#include "cmdline.h"
#include "jobctl.h"
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

bool exit_warning = false;

typedef struct {
    int pipefd[2];
    int closefd;
} context_t;

/// Internal functions
static context_t context_new(void);
static int conditional_execute(conditional_t *cond);
static int pipeline_execute(pipeline_t *pipeline, bool bg);
static int run_cmd(byte *const *argv, byte *const *env, context_t *ctx, job_t *job);
static int envcmd(byte *const *argv, int option);
static void penviron(void);
static void pbuiltin(void);
static void command_get_argv(command_t *cmd, byte *out[], size_t len);
static void command_get_env(command_t *cmd, byte *out[], size_t len);
static int close_pipe(int *pp);
static int add_envs_to_environ(byte *const *envp);
static void reset_signal_handling(void);
static void configure_pipes(size_t i, int *pipes, size_t cmdn, context_t *ctx);
static void resolve_redirections(byte *const *argvp);
static void connect_io_with_pipe(context_t *ctx);
static int run_builtin(const byte *command, byte *const *argv, bool shell);
bool is_builtin(const byte *command);
static int run_cmd_nofork(byte **argv, byte **env);
static void fargvs(vec_t *argv, byte **out);

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
    int ret;

    for(size_t i = 0; i < cndn; i++) {
        printf("running conditional [%ld]\n", i);
        ret = conditional_execute(vec_index(cnds, i));
        *status = (ret == FAILURE) ? 1 : ret;
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
        printf("Running pipeline [%ld]\n", i);
        ctn = pipeline_execute(pipeline, cond->is_background);

        if(ctn == FAILURE && IS_AND(pipeline->connection)) {
            return FAILURE;
        } else if(ctn == SUCCESS && IS_OR(pipeline->connection)) {
            return SUCCESS;
        }
    }

    return ctn;
}

static int pipeline_execute(pipeline_t *pipeline, bool bg)
{
    context_t ctx; /* Context, instructs forked proc which pipe stream to dup and close */
    vec_t *commands = pipeline->commands; /* All of the commands for this pipeline */
    size_t cmdn = vec_len(commands);      /* Amount of commands in this pipeline */
    int status = SUCCESS;                 /* Return status of this pipeline */
    pid_t cpid;                           /* child Process ID */
    command_t *cmd = NULL;
    job_t job = job_new(pipeline->connection, bg); /* New job for this pipeline */

    if(__glibc_unlikely(is_null(job.processes)))
        return FAILURE;

    unsigned pn = ((cmdn - 1) * 2); /* Size of pipe array */
    int pipes[pn];                  /* Pipe storage */

    /// Run the entire pipeline
    for(size_t i = 0; i < cmdn; i++) {

        ctx = context_new();
        cmd = vec_index(commands, i);

        size_t argc = vec_len(cmd->argv);
        byte *argv[argc + 1];
        command_get_argv(cmd, argv, argc);

        size_t envc = vec_len(cmd->env);
        byte *env[envc + 1];
        command_get_env(cmd, env, envc);

        if(cmdn > 1) {
            exit_warning = false; /* User is not exiting the shell */
            configure_pipes(i, pipes, cmdn, &ctx);
        } else if(job.foreground && is_builtin(argv[0])) {
            job_drop(&job);
            return run_cmd_nofork(argv, env);
        }

        if(__glibc_likely((cpid = run_cmd(argv, env, &ctx, &job)) != FAILURE)) {
            process_t temp_proc = process_new(cpid, NULL);
            fargvs(cmd->argv, &temp_proc.commandline);

            if(__glibc_unlikely(
                   is_null(temp_proc.commandline) || !job_add_process(&job, &temp_proc)))
            {
                job_drop(&job);
                exit(EXIT_FAILURE);
            }
        } else {
            job_drop(&job);
            exit(EXIT_FAILURE);
        }

        if(not_first(i) && __glibc_unlikely(close_pipe(pipe_at(PREV(i), pipes)) < 0)) {
            job_drop(&job);
            exit(EXIT_FAILURE);
        }
    }

    if(job.foreground)
        status = job_move_to_fg(&job, false);
    else {
        if(__glibc_unlikely(!joblist_push(&job))) {
            ATOMIC_PRINT(
                { pwarn("failed adding a job [PGID:%d] to the joblist", job.pgid); });
            exit(EXIT_FAILURE);
        }
        job_move_to_bg(&job, false);
    }

    return status;
}

static void reset_signal_handling(void)
{
    struct sigaction sigdfl_ac;
    sigemptyset(&sigdfl_ac.sa_mask);
    sigdfl_ac.sa_flags = 0;
    sigdfl_ac.sa_handler = SIG_DFL;
    sigaction(SIGINT, &sigdfl_ac, NULL);
    sigaction(SIGQUIT, &sigdfl_ac, NULL);
    sigaction(SIGTSTP, &sigdfl_ac, NULL);
    sigaction(SIGTTIN, &sigdfl_ac, NULL);
    sigaction(SIGTTOU, &sigdfl_ac, NULL);
    sigaction(SIGCHLD, &sigdfl_ac, NULL);
}

static int envcmd(byte *const *argv, int option)
{
    const byte *temp = NULL;
    int status = FAILURE;

    switch(option) {
        case ADD_ENV:
        case SET_ENV:
            if(__glibc_unlikely(
                   (status = setenv(argv[1], argv[2], (option == ADD_ENV ? 0 : 1))) < 0))
                ATOMIC_PRINT({ perr(); });
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
    ATOMIC_PRINT({
        for(size_t i = 0; i < BUILTINN; i++)
            printf("%s\n", builtin[i]);
    });
}

static void penviron(void)
{
    ATOMIC_PRINT({
        int i = 0;
        while(is_some(environ[i]))
            printf("%s\n", environ[i++]);
    });
}

static void command_get_argv(command_t *cmd, byte *out[], size_t len)
{
    size_t i;
    vec_t *argv = cmd->argv;
    for(i = 0; i < len; i++)
        out[i] = string_slice(vec_index(argv, i), 0);
    out[i] = NULL;
}

static void command_get_env(command_t *cmd, byte *out[], size_t len)
{
    size_t i;
    vec_t *env = cmd->env;
    for(i = 0; i < len; i++)
        out[i] = string_slice(vec_index(env, i), 0);
    out[i] = NULL;
}

static int close_pipe(int *pp)
{
    if(__glibc_unlikely(close(*pp) < 0 || close(*(pp + 1)) < 0)) {
        ATOMIC_PRINT({
            pwarn("failed to close pipe [Wfd:%d|Rfd:%d]", *pp, *(pp + 1));
            perr();
        });
        return FAILURE;
    }
    return SUCCESS;
}

static int add_envs_to_environ(byte *const *envp)
{
    while(is_some(*envp))
        if(__glibc_unlikely(putenv(*envp++) < 0)) {
            ATOMIC_PRINT({
                pwarn("failed adding env var to environ '%s'", *(envp - 1));
                perr();
            });
            return FAILURE;
        }

    return SUCCESS;
}

static int rm_envs_from_environ(byte *const *envp)
{
    while(is_some(*envp)) {
        byte *temp = strchr(*envp, '=');
        byte c = *temp;
        *temp = NULL_TERM;
        if(__glibc_unlikely(unsetenv(*envp) < 0)) {
            *temp = c;
            ATOMIC_PRINT({
                pwarn("failed removing env var from environ '%s'", *envp);
                perr();
            });
            return FAILURE;
        }
        *temp = c;
        ++envp;
    }

    return SUCCESS;
}

static void configure_pipes(size_t i, int *pipes, size_t cmdn, context_t *ctx)
{
    if(first(i)) {
        if(__glibc_unlikely(pipe(pipes) < 0)) {
            ATOMIC_PRINT({ perr(); });
            exit(EXIT_FAILURE);
        }
        ctx->pipefd[PIPE_W] = pipes[PIPE_W];
        ctx->closefd = pipes[PIPE_R];
    } else if(not_first(i) && not_last(i, cmdn)) {
        if(__glibc_unlikely(pipe(pipe_at(CURR(i), pipes)) < 0)) {
            ATOMIC_PRINT({ perr(); });
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
        strcat(commandline, string_ref(vec_index(argv, i)));
        if(i + 1 != len)
            strcat(commandline, " ");
    }

    *out = strdup(commandline);

    if(__glibc_unlikely(is_null(*out))) {
        ATOMIC_PRINT({
            pwarn("failed copying over the user input: %s", commandline);
            perr();
        });
    }
}

bool is_builtin(const byte *command)
{
    for(size_t i = 0; i < BUILTINN; i++)
        if(strcmp(command, builtin[i]) == 0)
            return true;
    return false;
}

static int run_cmd_nofork(byte **argv, byte **env)
{
    int status;

    if(__glibc_unlikely(add_envs_to_environ(env) < 0))
        exit(EXIT_FAILURE);

    resolve_redirections(argv);
    status = run_builtin(argv[0], argv, true);

    if(strcmp(argv[0], "exit") != 0 && exit_warning)
        exit_warning = false;

    if(__glibc_unlikely(rm_envs_from_environ(env) < 0))
        exit(EXIT_FAILURE);

    return status;
}

static int run_cmd(byte *const *argv, byte *const *env, context_t *ctx, job_t *job)
{
    pid_t childPID = fork();

    if(__glibc_unlikely(childPID == -1)) {
        ATOMIC_PRINT({
            pwarn("failed forking parent process [ID:%d]", getpid());
            perr();
        });
        return FAILURE;
    } else if(childPID == 0) {
        /// Add environment variables to environ
        if(__glibc_unlikely(add_envs_to_environ(env) < 0))
            exit(EXIT_FAILURE);

        pid_t pid = getpid(); /* Current process ID */

        /// If this is the first command set the gpid as pid
        /// and give it the terminal
        if(job->pgid == 0) {
            job->pgid = pid;
            if(__glibc_unlikely(job->foreground && tcsetpgrp(TERMINAL_FD, job->pgid) < 0))
            {
                ATOMIC_PRINT({
                    pwarn("failed giving terminal to process group [ID:%d]", job->pgid);
                    perr();
                });
                exit(EXIT_FAILURE);
            }
        }
        /// Move this process into the new process group
        ATOMIC_PRINT(
            { fprintf(stderr, "setting PID:%d into PGID:%d\n", pid, job->pgid); });
        if(__glibc_unlikely(setpgid(pid, job->pgid) < 0)) {
            ATOMIC_PRINT({
                pwarn(
                    "process [ID:%d] failed setting itself into process group [ID:%d]",
                    pid,
                    job->pgid);
                perr();
            });
            exit(EXIT_FAILURE);
        }

        /// Reset signal handling to default
        reset_signal_handling();
        /// Connect stdin and stdout to pipe
        connect_io_with_pipe(ctx);
        /// Parse and configure all the redirections (<, >, >>, 2>)
        resolve_redirections(argv);
        /// Try execute builtin command
        if(is_builtin(argv[0])) {
            _exit(run_builtin(argv[0], argv, false));
        }
        /// Execute the non-builtin command
        if(execvp(argv[0], argv) < 0) {
            ATOMIC_PRINT({
                pwarn("failed to run the command '%s'", argv[0]);
                if(errno == ENOENT)
                    ATOMIC_PRINT({
                        pwarn("couldn't find file '%s' to execute the command", argv[0]);
                    });
                else
                    ATOMIC_PRINT({ perr(); });
            });
            exit(EXIT_FAILURE);
        }
    }

    if(job->pgid == 0)
        job->pgid = childPID;

    ATOMIC_PRINT(
        { fprintf(stderr, "setting PID:%d into PGID:%d\n", childPID, job->pgid); });

    if(__glibc_unlikely(setpgid(childPID, job->pgid) < 0)) {
        ATOMIC_PRINT({
            pwarn(
                "process [ID:%d] failed setting itself into process group [ID:%d]",
                childPID,
                job->pgid);
            perr();
        });
        exit(EXIT_FAILURE);
    }

    return childPID;
}

static void connect_io_with_pipe(context_t *ctx)
{
    /// Connect pipe streams and close the
    /// redundant pipe end, if any
    if(__glibc_unlikely(
           dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0
           || dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0
           || (ctx->closefd != -1 && close(ctx->closefd) < 0)))
    {
        ATOMIC_PRINT({ perr(); });
        exit(EXIT_FAILURE);
    }
}

static void resolve_redirections(byte *const *argvp)
{
    /// Default streams
    int infd = STDIN_FILENO;   /* Default input stream */
    int outfd = STDOUT_FILENO; /* Default output stream */
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

                if(__glibc_unlikely((redfd = openf(*(++argvp), append)) < 0)) {
                    ATOMIC_PRINT({
                        pwarn("failed to open a file '%s'", *argvp);
                        perr();
                    });
                    exit(EXIT_FAILURE);
                } else if(__glibc_unlikely(close(outfd) < -1)) {
                    ATOMIC_PRINT({
                        pwarn("failed to close fd '%d'", outfd);
                        perr();
                    });
                    exit(EXIT_FAILURE);
                } else
                    outfd = redfd;
                break;
            case '<':
                if(__glibc_unlikely((redfd = open(*(++argvp), O_RDONLY)) < 0)) {
                    ATOMIC_PRINT({
                        pwarn("failed to open a file '%s'", *argvp);
                        perr();
                    });
                    exit(EXIT_FAILURE);
                } else if(__glibc_unlikely(close(infd) < 0)) {
                    ATOMIC_PRINT({
                        pwarn("failed to close fd '%d'", infd);
                        perr();
                    });
                    exit(EXIT_FAILURE);
                } else
                    infd = redfd;
                break;
            case '2':
                if(*(*argvp + 1) == '>') {
                    if(*(*argvp + 2) == '>')
                        append = true;

                    if(__glibc_unlikely((redfd = openf(*(++argvp), append)) < 0)) {
                        ATOMIC_PRINT({
                            pwarn("failed to open a file '%s'", *argvp);
                            perr();
                        });
                        exit(EXIT_FAILURE);
                    } else if(__glibc_unlikely(close(errfd) == -1)) {
                        ATOMIC_PRINT({
                            pwarn("failed to close fd '%d'", errfd);
                            perr();
                        });
                        exit(EXIT_FAILURE);
                    } else
                        errfd = redfd;
                    break;
                }
                // FALLTHRU
            default:
                *pargvp++ = *argvp;
                break;
        }
        append = false;
    }
    *pargvp = NULL; /* Null out the pruned argv */

    /// Ensure all the redirections are taken care of
    if(__glibc_unlikely(
           dup2(infd, STDIN_FILENO) == -1 || dup2(outfd, STDOUT_FILENO) == -1
           || dup2(errfd, STDERR_FILENO) == -1))
    {
        ATOMIC_PRINT({ perr(); });
        exit(EXIT_FAILURE);
    }
}

static int run_builtin(const byte *command, byte *const *argv, bool shell)
{
    int status = FAILURE;
    if(strcmp(command, "exit") == 0) {
        if(is_null(argv[1])) {
            if(shell) {
                if(joblist_len() != 0 && !exit_warning) {
                    exit_warning = true;
                    ATOMIC_PRINT({
                        pwarn("There are still background jobs running!\nRun "
                              "\x1B[32mexit\x1b[0m again to exit, this will result in "
                              "orphaned child processes.");
                    });
                } else
                    exit(SUCCESS);
            }
            status = SUCCESS;
        } else if(is_some(argv[1]) && is_null(argv[2])) {
            int exit_status;
            if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
                ATOMIC_PRINT({
                    fprintf(stderr, "exit - exits the shell");
                    pusage("error [CODE]");
                    fprintf(
                        stderr,
                        "\n\x1B[32mexit\x1B[0m is a builtin command that exits the shell "
                        "with the CODE if "
                        "supplied or 0 in case CODE is not supplied. In case there are "
                        "background "
                        "jobs running when exit is called, shell will not exit "
                        "instantly, "
                        "instead it will print additional information and then the user "
                        "can "
                        "exit by calling exit again.");
                });
            } else {
                exit_status = strtol(argv[1], NULL, 10);
                if(errno == EINVAL || errno == ERANGE) {
                    ATOMIC_PRINT({
                        pwarn("invalid exit status integer");
                        pusage("error [CODE]");
                        fprintf(
                            stderr,
                            "\nThe -h or --help options display help information this "
                            "command\n");
                    });
                } else {
                    status = exit_status;
                    if(shell) {
                        if(joblist_len() != 0 && !exit_warning) {
                            exit_warning = true;
                            ATOMIC_PRINT({
                                pwarn("There are still background jobs running!\nRun "
                                      "\x1B[32mexit\x1b[0m again to exit, this will "
                                      "result in orphaned child processes.");
                            });
                        } else
                            exit(exit_status);
                    }
                }
            }
        } else {
            ATOMIC_PRINT({
                PW_TOO_MANY(argv[0]);
                pusage("exit [CODE]");
            });
            status = FAILURE;
        }
    } else if(strcmp(command, "cd") == 0) {
        if(is_null(argv[1])) {
            if(__glibc_unlikely(chdir(getenv(HOME)) == -1)) {
                ATOMIC_PRINT({ perr(); });
                status = FAILURE;
            } else
                status = SUCCESS;
        } else if(is_some(argv[2])) {
            ATOMIC_PRINT({
                PW_TOO_MANY(argv[0]);
                pusage("cd [DIRNAME]");
            });
            status = FAILURE;
        } else if(chdir(argv[1]) < 0) {
            ATOMIC_PRINT({
                if(errno == ENOENT)
                    pwarn("couldn't find path '%s'", argv[1]);
                perr();
            });
            status = FAILURE;
        } else
            status = SUCCESS;
    } else if(strcmp(command, "penv") == 0) {
        if(is_null(argv[1])) {
            status = envcmd(NULL, P_ALL);
        } else if(is_some(argv[1]) && is_null(argv[2])) {
            status = envcmd(argv, P_ENV);
        } else {
            ATOMIC_PRINT({
                PW_TOO_MANY(argv[0]);
                pusage("penv [VARNAME]");
            });
            status = FAILURE;
        }
    } else if(strcmp(command, "senv") == 0) {
        if(is_null(argv[1]) || is_null(argv[2])) {
            ATOMIC_PRINT({
                PW_TOO_FEW(argv[0]);
                pusage("senv [VARNAME] [VARVALUE]");
            });
            status = FAILURE;
        } else if(is_some(argv[3])) {
            ATOMIC_PRINT({
                PW_TOO_MANY(argv[0]);
                pusage("senv [VARNAME] [VARVALUE]");
            });
            status = FAILURE;
        } else
            status = envcmd(argv, SET_ENV);
    } else if(strcmp(command, "renv") == 0) {
        if(is_null(argv[1])) {
            ATOMIC_PRINT({
                PW_TOO_FEW(argv[0]);
                pusage("renv [VARNAME] [VARVALUE]");
            });
            status = FAILURE;
        } else if(is_some(argv[2])) {
            ATOMIC_PRINT({
                PW_TOO_MANY(argv[0]);
                pusage("renv [VARNAME] [VARVALUE]");
            });
            status = FAILURE;
        } else
            status = envcmd(argv, RM_ENV);
    } else if(strcmp(command, "pwd") == 0) {
        byte buff[PATH_MAX];
        if(__glibc_unlikely(is_null(getcwd(buff, PATH_MAX)))) {
            ATOMIC_PRINT({ perr(); });
            status = FAILURE;
        } else {
            printf("%s\n", buff);
            status = SUCCESS;
        }
    } else if(strcmp(command, "clear") == 0) {
        status = system("clear");
    } else if(strcmp(command, "builtin") == 0) {
        if(is_some(argv[1])) {
            ATOMIC_PRINT({
                PW_TOO_MANY(argv[0]);
                pusage("builtin");
            });
            status = FAILURE;
        } else {
            pbuiltin();
            status = SUCCESS;
        }
    }
    return status;
}
