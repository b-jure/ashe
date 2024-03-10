#include "abuiltin.h"
#include "aerrors.h"
#include "ajobcntl.h"
#include "aparser.h"
#include "arun.h"

#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>



#define DEV_DIR "/dev/"
#define FD_DIR  DEV_DIR "fd/" 

static const char* specialfiles[] = {
    "stdin",
    "stdout",
    "stderr",
};



#define PIPE_R 0 /* Read end of a pipe */
#define PIPE_W 1 /* Write end of a pipe */



/* open file for writing '>' or '>>' */
#define ashe_wopen(file, append) \
    open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)

/* open file for reading '<' */
#define ashe_ropen(file) open(file, O_RDONLY)

/* open file for reading and writing '<>' */
#define ashe_rwopen(file) open(file, O_CREAT | O_RDWR, 0666)



/* runtime errors */
#define ERR_FDNOTOPEN 0
#define ERR_FDLIMIT 0
static const char* runerrors[] = {
    "file descriptor '%d' is not open, fallback to default.",
    "file descriptor '%d' is above the system limit (sysconf(_SC_OPEN_MAX)).",
};



/* Instructs forked process which pipe stream to dup() and close. */
typedef struct {
    int32 pipefd[2];
    int32 closefd;
} PipeContext;


static finline void Context_init(PipeContext* ctx)
{
    ctx->pipefd[0] = STDIN_FILENO;
    ctx->pipefd[1] = STDOUT_FILENO;
    ctx->closefd = -1;
}


static void configure_pipe_at(int32* pipes, memmax len, memmax i, PipeContext* ctx)
{
    int32* poffset = NULL;
    if(i == 0) {
        poffset = pipes;
        if(unlikely(pipe(poffset) < 0)) die();
        else {
            ctx->pipefd[PIPE_W] = poffset[PIPE_W];
            ctx->closefd = poffset[PIPE_R];
        }
    } else if(i != len - 1) {
        poffset = &pipes[i * 2];
        if(unlikely(pipe(poffset) < 0)) die();
        else {
            ctx->pipefd[PIPE_R] = poffset[-2];
            ctx->pipefd[PIPE_W] = poffset[PIPE_W];
            ctx->closefd = poffset[PIPE_R];
        }
    } else {
        poffset = &pipes[--i * 2];
        ctx->pipefd[PIPE_R] = poffset[PIPE_R];
        ctx->closefd = poffset[PIPE_W];
    }
}


static int32 add_envvars(const ArrayCharptr* env)
{
    memmax len = env->len;
    for(memmax i = 0; i < len; i++) {
        char* name = env->data[i];
        char* sep = strchr(name, '=');
        char* value = sep + 1;
        *sep = '\0';
        if(unlikely(setenv(name, value, 1) < 0)) {
            printf_error("failed exporting variable '%s'.", name);
            print_errno();
            return -1;
        }
    }
    return 0;
}


static finline ubyte fd_isvalid(int32 fd)
{
    return (fcntl(fd, F_GETFD) != -1 || errno != EBADF);
}

/* Auxiliary to 'resolve_fdcs()' */
static void setfd(int32* fdp, ubyte write)
{
    int32 fd = *fdp;
    if(fd == -1) return;
    if(!fd_isvalid(fd)) {
        int64 fdmax = sysconf(_SC_OPEN_MAX);
        ubyte error = ERR_FDNOTOPEN;
        if(fdmax > fd) error = ERR_FDLIMIT;
        fprint_warning(runerrors[error], fd);
        *fdp = (write ? STDOUT_FILENO : STDIN_FILENO);
    }
}


/* Resolves file descriptor context array by setting/closing
 * the file descriptors accordingly and resolving the redirections.
 * Note: this is invoked either just before executing builtin command
 * or inside of a forked process, meaning changes to file handlers (file descriptors)
 * inside of the current process are permanent (until program end). */
static int32 cmd_resolve_redirections(Command* cmd)
{
    ArrayFDContext* fdcs = &cmd->fdcs;
    ubyte exec = strcmp(cmd->argv.data[0].data, "exec") == 0;
    for(memmax i = 0; i < fdcs->len; i++) {
        FileHandle* fdc = ArrayFDContext_index(fdcs, i);
        if(fdc->close && exec) { // close file descriptor ?

        } else {
            setfd(&fdc->fd_left, fdc->write);
            setfd(&fdc->fd_right, fdc->write);
            if(fdc->file.cap != 0) { // file redirection
                if(fdc->write) fdc->fd_right = ashe_wopen(fdc->file.data, fdc->append);
                else fdc->fd_right = ashe_ropen(fdc->file.data);
                if(fdc->fd_right < 0) {
                    print_errno();
                    return -1;
                }
            }
        }
    }
    return 0;
}


static int32 run_cmd_nofork(Command* cmd)
{
    int32 status, out, err, in;
    ArrayBuffer* env = &cmd->env;
    ArrayBuffer* argv = &cmd->argv;
    status = 0;

    if(unlikely(add_envvars(env) == -1)) die();
    if(argv->len == 0) return status;

    in = STDIN_FILENO;
    out = STDOUT_FILENO;
    err = STDERR_FILENO;

    /* dup() default descriptors in order to restore them later */
    if(unlikely((in = dup(STDIN_FILENO)) < 0 || (out = dup(STDOUT_FILENO)) < 0 || (err = dup(STDERR_FILENO)) < 0))
        die();

    ArrayFDContext* rds = &cmd->fdcs;
    cmd_resolve_redirections(argv);
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


static int32 run_cmd(byte* const* argv, byte* const* env, PipeContext* ctx, Job* job)
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
        if(unlikely(add_envvars(env) < 0)) _exit(EXIT_FAILURE);

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
        cmd_resolve_redirections(argv);

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
        PipeContext ctx;
        Context_init(&ctx);
        Command* cmd = ArrayCommand_index(commands, i);

        if(cmdn > 1) {
            ashe.sh_exit = 0; /* User is not exiting the shell */
            configure_pipe_at(i, pipes, cmdn, &ctx);
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

static int32 close_pipe(int32* pipe)
{
    if(unlikely(close(pipe[PIPE_R]) < 0 || close(pipe[PIPE_W]) < 0)) {
        PW_CPIPE(pipe);
        perr();
        return -1;
    }
    return 0;
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

static void connect_io_with_pipe(PipeContext* ctx)
{
    if(unlikely(
           dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0 ||
           dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0 ||
           (ctx->closefd != -1 && close(ctx->closefd) < 0)))
    {
        ATOMIC_PRINT(die());
    }

}

