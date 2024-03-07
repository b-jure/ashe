#include "ashe_string.h"
#include "builtin.h"
#include "errors.h"
#include "jobctl.h"
#include "parser.h"
#include "vec.h"
#include "cmdline.h"

#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>


#define PIPE_R 0 /* Read  end of a pipe */
#define PIPE_W 1 /* Write end of a pipe */


/* Default modes for opening a file in which we are redirecting the output */
#define openf(file, append) open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)


/* Instructs forked process which pipe stream to dup() and close. */
typedef struct {
    int32 pipefd[2];
    int32 closefd;
} Context;


static finline void Context_init(Context* ctx)
{
    ctx->pipefd[0] = STDIN_FILENO;
    ctx->pipefd[1] = STDOUT_FILENO;
    ctx->closefd = -1;
}


static void configure_pipes(memmax i, int32* pipes, memmax cmdn, Context* ctx)
{
// TODO: Fix
#define pipe_at(i, pipes) (pipes + (i * 2))
    if(i == 0) {
        if(unlikely(pipe(pipes) < 0)) {
            die();
        } else {
            ctx->pipefd[PIPE_W] = pipes[PIPE_W];
            ctx->closefd = pipes[PIPE_R];
        }
    } else if(i != 0 && i + 1 != cmdn) {
        if(unlikely(pipe(&pipes[i * 2]) < 0)) {
            die();
        } else {
            ctx->pipefd[PIPE_R] = *pipe_at(i - 1, pipes);
            ctx->pipefd[PIPE_W] = *(pipe_at(i, pipes) + PIPE_W);
            ctx->closefd = *(pipe_at(i - 1, pipes) + PIPE_W);
        }
    } else {
        ctx->pipefd[PIPE_R] = *pipe_at(i - 1, pipes);
        ctx->closefd = *(pipe_at(i - 1, pipes) + PIPE_W);
    }

#undef pipe_at
}


static finline ubyte fd_is_valid(int32 fd)
{
    return (fcntl(fd, F_GETFD) != -1 || errno != EBADF);
}


static int32 add_envs_to_environ(const ArrayBuffer* env)
{
    memmax len = env->len;
    for(memmax i = 0; i < len; i++) {
        char* name = ArrayBuffer_index(env, i)->data;
        char* sep = strchr(name, '=');
        char* value = sep + 1;
        *sep = '\0';
        if(unlikely(setenv(name, value, 1) < 0)) {
            print_warning("failed exporting variable" bold(bred(" %s")), name);
            print_errno();
            return -1;
        }
    }
    return 0;
}


static int32 run_cmd_nofork(ArrayBuffer* argv, ArrayBuffer* env)
{
    int32 status, out, err, in;
    status = 0;

    if(unlikely(add_envs_to_environ(env) == -1)) {
        die();
    }

    /// Early return if there are no commands
    /// to execute (no pipelines, conditionals, ...)
    if(argv->len == 0) {
        return status;
    }

    in = STDIN_FILENO;
    out = STDOUT_FILENO;
    err = STDERR_FILENO;

    if(unlikely(
           (in = dup(STDIN_FILENO)) < 0 || (out = dup(STDOUT_FILENO)) < 0 ||
           (err = dup(STDERR_FILENO)) < 0))
    {
        die();
    }

    resolve_redirections(argv);
    status = run_builtin(argv[0], argv, 1);

    if(unlikely(
           dup2(in, STDIN_FILENO) < 0 || dup2(out, STDOUT_FILENO) < 0 ||
           dup2(err, STDERR_FILENO) < 0))
    {
        PW_SHDUP;
        die();
    }

    if(strcmp(argv[0], "exit") != 0 && exit_warning) exit_warning = 0;

    if(unlikely(rm_envs_from_environ(env) < 0)) exit(EXIT_FAILURE);

    return status;
}


static int32 run_cmd(byte* const* argv, byte* const* env, Context* ctx, Job* job)
{
    pid_t childPID = fork();

    if(unlikely(childPID == -1)) {
        job_drop(job);
        ATOMIC_PRINT({
            PW_FORKERR;
            die();
        });
    } else if(childPID == 0) {
        /// NOTE**
        /// Forked process must not call atexit() registered
        /// functions, that is why we use _exit and _die.
        /// Reason being is that the forked process gets exact copy of
        /// the global shell structure, this means it also contains
        /// exact copy of joblist structure which holds valid
        /// PGIDs inside. This in combination that atexit registered
        /// shell cleanup function sends a kill signal to all the processes inside
        /// a joblist therefore removing and harvesting all the jobs.
        /// Because we are also running builtin commands in the fork and/or
        /// something might fail such as exec call then the program might/will exit
        /// triggering atexit registered functions.

        /// If no command were provided and only
        /// env vars then exit immediately
        if(unlikely(is_null(argv[0]))) _exit(EXIT_SUCCESS);

        /// Export env variables
        if(unlikely(add_envs_to_environ(env) < 0)) _exit(EXIT_FAILURE);

        pid_t pid = getpid(); /* Current process ID */

        /// If this is the first command set the gpid as pid
        /// and give it the terminal
        if(job->pgid == 0) {
            job->pgid = pid;
            if(unlikely(job->foreground && tcsetpgrp(TERMINAL_FD, job->pgid) < 0)) {
                ATOMIC_PRINT({
                    PW_TERMTCSET(job->pgid);
                    _die();
                });
            }
        }

        /// Move this process into the new process group
        if(unlikely(setpgid(pid, job->pgid) < 0)) {
            ATOMIC_PRINT({
                PW_PGRPSET(pid, job->pgid);
                _die();
            });
        }

        /// Reset signal handling to default (async)
        reset_signal_handling();
        /// Connect stdin and stdout to pipe
        connect_io_with_pipe(ctx);
        /// Parse and configure all the redirections
        resolve_redirections(argv);

        /// If builtin then execute
        if(is_builtin(argv[0])) _exit(run_builtin(argv[0], argv, 0));

        /// Try exec the non-builtin command
        if(execvp(argv[0], argv) < 0) {
            ATOMIC_PRINT({
                PW_EXECERR(argv[0]);
                if(errno == ENOENT) PW_NOFILE(argv[0]);
                else perr();
            });
            _exit(EXIT_FAILURE);
        }
    }

    if(job->pgid == 0) job->pgid = childPID;

    /// Same here, move the process into job process group
    /// this is also done in the fork to prevent race
    if(unlikely(setpgid(childPID, job->pgid) < 0)) {
        ATOMIC_PRINT({
            PW_PGRPSET(childPID, job->pgid);
            die();
        });
    }

    return childPID;
}



static int32 Pipeline_run(Pipeline* pipeline, ubyte bg)
{
    ArrayCommand* commands = &pipeline->commands;
    memmax cmdn = commands->len;
    int32 status = 0; /* return status of this pipeline */
    pid_t cpid; /* child process ID */
    Job job; /* pipeline job */
    Job_init(&job, pipeline->connection, bg);

    ashe_assert(cmdn >= 0, "pipeline has no commands to run");

    uint32 pn = ((cmdn - 1) * 2); /* size of pipe array */
    pn = (pn == 0 ? 1 : pn); /* check in case of 0 */
    int32 pipes[pn]; /* pipe storage */

    /// Run the entire pipeline
    for(memmax i = 0; i < cmdn; i++) {
        Context ctx;
        Context_init(&ctx);
        Command* cmd = ArrayCommand_index(commands, i);

        if(cmdn > 1) {
            ashe.sh_exit = 0; /* User is not exiting the shell */
            configure_pipes(i, pipes, cmdn, &ctx);
        } else if(job.foreground && (cmd->argv.len == 0 || is_builtin(cmd->argv.data->data))) {
            /// In case we don't have a pipeline or conditional and
            /// only a single command that is run in foreground and
            /// is a builtin command or only contains environment variables,
            /// then run the command without forking.
            Job_free(&job);
            return run_cmd_nofork(&cmd->argv, &cmd->env);
        }

        cpid = run_cmd(argv, env, &ctx, &job);

        Process temp_proc = process_new(cpid, NULL);
        const char* argvstr = Command_argvstr(&cmd);
        argv_to_str(&cmd->argv, temp_proc.commandline);

        if(unlikely(
               is_null(temp_proc.commandline) || !job_add_process(&job, &temp_proc) ||
               (not_first(i) && close_pipe(pipe_at(PREV(i), pipes)) < 0)))
        {
            job_drop(&job);
            exit(EXIT_FAILURE);
        }
    }

    if(job.foreground) {
        status = job_move_to_fg(&job, 0);
    } else {
        if(unlikely(!joblist_push(&joblist, &job))) {
            ATOMIC_PRINT(PW_ADDJ(&job));
            exit(EXIT_FAILURE);
        }
        job_move_to_bg(&job, 0);
    }

    return status;
}


static int32 Conditional_run(Conditional* cond)
{
    ArrayPipeline* pipes = &cond->pipelines;
    int32 status = 0;
    for(memmax i = 0; i < pipes->len; i++) {
        Pipeline* pipeline = ArrayPipeline_index(pipes, i);
        status = Pipeline_run(pipeline, cond->is_background);
        status += pipeline->connection;
        if(status == 1 || status == 4) return status;
    }
    return status;
}


int32 cmdexec(ArrayConditional* conds) {
    int32 status = 0;
    for(memmax i = 0; i < conds->len; i++) {
        Conditional* cond = ArrayConditional_index(conds, i);
        status = -Conditional_run(cond);
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

static void Command_getargv(Command* cmd, byte* out[])
{
    ArrayByteArray* argv = &cmd->argv;
    memmax i;
    for(i = 0; i < argv->len; i++) {
        ByteArray* buffer = ArrayByteArray_index(argv, i);
        out[i] = out[i] = string_slice(vec_index(argv, i), 0);
    }
    out[i] = NULL;
}

static void Command_getenv(Command* cmd, byte* out[], memmax len)
{
    memmax i;
    vec_t* env = cmd->env;

    if(is_some(env)) {
        for(i = 0; i < len; i++)
            out[i] = string_slice(vec_index(env, i), 0);
        out[i] = NULL;
    }
}

static int32 close_pipe(int32* pp)
{
    if(unlikely(close(*pp) < 0 || close(*(pp + 1)) < 0)) {
        ATOMIC_PRINT({
            PW_CPIPE(pp);
            perr();
        });
        return FAILURE;
    }
    return SUCCESS;
}

static int32 rm_envs_from_environ(byte* const* envp)
{
    while(is_some(*envp)) {
        byte* temp = strchr(*envp, '=');
        byte c = *temp;
        *temp = NULL_TERM;

        if(unlikely(unsetenv(*envp) < 0)) {
            *temp = c;
            ATOMIC_PRINT({
                PW_VARRM(envp);
                perr();
            });
            return FAILURE;
        }
        *temp = c;
        ++envp;
    }

    return SUCCESS;
}

static void connect_io_with_pipe(Context* ctx)
{
    if(unlikely(
           dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0 ||
           dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0 ||
           (ctx->closefd != -1 && close(ctx->closefd) < 0)))
    {
        ATOMIC_PRINT(die());
    }

}

static void resolve_redirections(byte* const* argvp)
{
    /// Default streams
    int32 infd = STDIN_FILENO; /* Default input stream */
    int32 outfd = STDOUT_FILENO; /* Default output stream */
    int32 errfd = STDERR_FILENO; /* Default err stream */
    int32 redfd = -1; /* Redirections fd */

    /// Parse redirections and also prune out argv leaving
    /// only command and its arguments without redirections
    ubyte append = 0;
    const byte** pargvp = (const byte**)argvp;

    for(; is_some(*argvp); argvp++) {
        switch(**argvp) {
            case '>':
                if(*(*argvp + 1) == '>') append = 1;

                if(unlikely((redfd = openf(*(++argvp), append)) < 0)) {
                    ATOMIC_PRINT({
                        PW_OPENF(*argvp);
                        die();
                    });
                } else if(unlikely(close(outfd) < -1)) {
                    ATOMIC_PRINT({
                        PW_CLOSEF(outfd);
                        die();
                    });
                } else outfd = redfd;
                break;
            case '<':
                if(unlikely((redfd = open(*(++argvp), O_RDONLY)) < 0)) {
                    ATOMIC_PRINT({
                        PW_OPENF(*argvp);
                        die();
                    });
                } else if(unlikely(close(infd) < 0)) {
                    ATOMIC_PRINT({
                        PW_CLOSEF(infd);
                        die();
                    });
                } else infd = redfd;
                break;
            case '2':
                if(*(*argvp + 1) == '>') {
                    if(*(*argvp + 2) == '>') append = 1;

                    if(unlikely((redfd = openf(*(++argvp), append)) < 0)) {
                        ATOMIC_PRINT({
                            PW_OPENF(*argvp);
                            die();
                        });
                    } else if(unlikely(close(errfd) == -1)) {
                        ATOMIC_PRINT({
                            PW_CLOSEF(errfd);
                            die();
                        });
                    } else errfd = redfd;
                    break;
                }
                // FALLTHRU
            default: *pargvp++ = *argvp; break;
        }
        append = 0;
    }
    *pargvp = NULL; /* Null out the pruned argv */

    /// Ensure all the redirections are taken care of
    if(unlikely(
           dup2(infd, STDIN_FILENO) == -1 || dup2(outfd, STDOUT_FILENO) == -1 ||
           dup2(errfd, STDERR_FILENO) == -1))
    {
        ATOMIC_PRINT(die());
    }
}
