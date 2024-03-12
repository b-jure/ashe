#include "abuiltin.h"
#include "ajobcntl.h"
#include "autils.h"
#include "aparser.h"
#include "arun.h"
#include "ashell.h"

#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>



#define DEV_DIR "/dev/"
ASHE_PRIVATE const char* devfiles[] = {
    DEV_DIR "stdin",
    DEV_DIR "stdout",
    DEV_DIR "stderr",
};

#define DEVFILES_N (sizeof(devfiles) / sizeof(devfiles[0]))



#define PIPE_R 0 /* Read end of a pipe */
#define PIPE_W 1 /* Write end of a pipe */



/* Reserved file descriptors */
#ifndef ASHE_FD_0
#define ASHE_FD_0 10
#endif
#ifndef ASHE_FD_1
#define ASHE_FD_1 11
#endif
#ifndef ASHE_FD_2
#define ASHE_FD_2 12
#endif



/* open file for writing '>' or '>>' */
#define ashe_wopen(file, append) \
    open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)

/* open file for reading '<' */
#define ashe_ropen(file) open(file, O_RDONLY)

/* open file for reading and writing '<>' */
#define ashe_rwopen(file, append) \
    open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_RDWR, 0666)




#define ERR_FDNOTOPEN   0
#define ERR_FDLIMIT     1
#define ERR_VAREXPORT   2
#define ERR_VARRM       3
#define ERR_BADFD       4
ASHE_PRIVATE const char* runerrors[] = {
    "file descriptor '%d' is not open, fallback to default.",
    "file descriptor '%d' is above the system limit (sysconf(_SC_OPEN_MAX)).",
    "failed exporting variable '%s'.",
    "couldn't remove variable '%s'.",
    "bad file descriptor '%zu'.",
};




/* Instructs forked process which pipe stream to dup() and close. */
typedef struct {
    int32 pipefd[2];
    int32 closefd;
} PipeContext;


ASHE_PRIVATE finline void Context_init(PipeContext* ctx)
{
    ctx->pipefd[0] = STDIN_FILENO;
    ctx->pipefd[1] = STDOUT_FILENO;
    ctx->closefd = -1;
}


ASHE_PRIVATE void configure_pipe_at(int32* pipes, memmax len, memmax i, PipeContext* ctx)
{
    int32* poffset = NULL;
    if(i == 0) {
        poffset = pipes;
        if(unlikely(pipe(poffset) < 0)) {
            print_errno();
            panic(NULL);
        } else {
            ctx->pipefd[PIPE_W] = poffset[PIPE_W];
            ctx->closefd = poffset[PIPE_R];
        }
    } else if(i != len - 1) {
        poffset = &pipes[i * 2];
        if(unlikely(pipe(poffset) < 0)) {
            print_errno();
            panic(NULL);
        } else {
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


ASHE_PRIVATE int32 try_add_envvars(ArrayCharptr* env)
{
    memmax len = env->len;
    for(memmax i = 0; i < len; i++) {
        char* name = env->data[i];
        char* sep = strchr(name, '=');
        char* value = sep + 1;
        *sep = '\0';
        if(unlikely(setenv(name, value, 1) < 0)) {
            printf_error(runerrors[ERR_VAREXPORT], name);
            print_errno();
            return -1;
        }
    }
    return 0;
}

ASHE_PRIVATE int32 try_rm_envvars(ArrayCharptr* env)
{
    int32 status = 0;
    memmax len = env->len;
    for(memmax i = 0; i < len; i++) {
        char* name = env->data[i];
        // name is already in format 'key\0value'
        if(unlikely(unsetenv(name) < 0)) { // can this ever fail ?
            printf_error(runerrors[ERR_VARRM], name);
            print_errno();
            status = -1;
            continue; // try remove the rest
        }
    }
    return status;
}


ASHE_PRIVATE finline ubyte fd_isvalid(int32 fd)
{
    return (fcntl(fd, F_GETFD) != -1 || errno != EBADF);
}

ASHE_PRIVATE finline int32 filepath_is_fd(const char* filepath)
{
    for(memmax i = 0; i < DEVFILES_N; i++) {
        if(strcmp(filepath, devfiles[i]) != 0) continue;
        if(fd_isvalid(i)) return i;
        break;
    }
    return -1;
}

ASHE_PRIVATE finline ubyte redirect_stderrout_into(int32 fd)
{
    if((dup2(STDOUT_FILENO, STDERR_FILENO) == 0 && dup2(fd, STDOUT_FILENO) == 0)) {
        print_errno();
        return -1;
    }
    return 0;
}

ASHE_PRIVATE finline ubyte redirect_fd_into(int32 fd, int32 to)
{
    if(dup2(to, fd) < 0) {
        print_errno();
        return -1;
    }
    return 0;
}

ASHE_PRIVATE int32 try_resolve_redirections(ArrayFileHandle* fhs)
{
    ssize fd;
    memmax len = fhs->len;
    int32 opened_files[len];
    int32 filecnt = 0;

    for(memmax i = 0; i < len; i++) {
        FileHandle* fh = &fhs->data[i];
        switch(fh->op) {
            case OP_REDIRECT_ERROUT: {
                fd = filepath_is_fd(fh->filepath);
                if(fd < 0) { 
                    fd = ashe_wopen(fh->filepath, fh->append);
                    if(fd < 0) {
                        print_errno();
                        goto l_error_cleanup;
                    }
                    opened_files[filecnt++] = fd;
                }
                if(!fd_isvalid(fd)) {
                    printf_error(runerrors[ERR_BADFD], (memmax)fd);
                } else if(redirect_stderrout_into(fd)) {
                    break;
                }
                goto l_error_cleanup;
            }
            case OP_REDIRECT_INOUT:
            case OP_REDIRECT_IN:
            case OP_REDIRECT_OUT: {
                fd = filepath_is_fd(fh->filepath);
                if(fd < 0) {
                    if(fh->op == OP_REDIRECT_IN) {
                        fd = ashe_ropen(fh->filepath);
                    } else if(fh->op == OP_REDIRECT_OUT) {
                        fd = ashe_wopen(fh->filepath, fh->append);
                    } else {
                        fd = ashe_rwopen(fh->filepath, 0);
                    }
                    if(fd < 0) {
                        print_errno();
                        goto l_error_cleanup;
                    }
                    opened_files[filecnt++] = fd;
                }
                if(fh->fd > INT_MAX || !fd_isvalid(fh->fd)) {
                    printf_error(runerrors[ERR_BADFD], fh->fd);
                    goto l_error_cleanup;
                } else if(!redirect_fd_into(fh->fd, fd)) {
                    goto l_error_cleanup;
                }
                break;
            }
            case OP_DUP_IN:
            case OP_DUP_OUT:
            case OP_CLOSE: { // these are resolved in 'exec' builtin or are ignored
                break;
            }
        }
    }

    return 0;

    l_error_cleanup:
    for(int32 i = 0; i < filecnt; i++) // try all opened files
        if(unlikely(close(opened_files[i]) < 0)) print_errno();

    return -1;
}


/* This runs a built-in command or puts environment
 * variables into 'environ' or both. */
ASHE_PRIVATE int32 run_cmd_nofork(Command* cmd)
{
    int32 i, o, e; // control variables
    int32 status, out, err, in;
    ArrayCharptr* env = &cmd->env;
    ArrayCharptr* argv = &cmd->argv;
    ArrayFileHandle* fhs = &cmd->fhandles;

    /* These are reserved fd's hopefully user will respect this
     * otherwise we might not be able to restore standard file descriptors. */
    in = ASHE_FD_0; out = ASHE_FD_1; err = ASHE_FD_2;

    /* Add environment variables to 'environ' */
    if(unlikely(try_add_envvars(env) == -1))
        return -2;

    /* If no commands to run just return */
    if(argv->len == 0) 
        return 0;

    l_dup_again:
    if(unlikely(dup2(STDIN_FILENO, in) < 0 || dup2(STDOUT_FILENO, out) < 0 || dup2(STDERR_FILENO, err) < 0)) {
        if(unlikely(errno == EINTR)) {
            errno = 0;
            goto l_dup_again;
        }
        printf_error("couldn't backup standard file descriptor.");
        goto l_close_duplicates;
    }

    if(try_resolve_redirections(fhs) < 0) {
        status = -2;
        goto l_restore_fds;
    }

    status = run_builtin(argv);

    l_restore_fds:
    i = o = e = -1;
    if(unlikely((i = dup2(in, STDIN_FILENO)) < 0 || (o = dup2(out, STDOUT_FILENO)) < 0 || (e = dup2(err, STDERR_FILENO)) < 0))
    {
        l_try_again:
        print_errno();
        errno = 0;

        if(unlikely(i++ == 0 && dup2(in, STDIN_FILENO)  < 0)) { i--; goto l_check_dup_error; }
        if(unlikely(o++ == 0 && dup2(in, STDOUT_FILENO) < 0)) { o--; goto l_check_dup_error; }
        if(unlikely(e++ == 0 && dup2(in, STDERR_FILENO) < 0)) { e--; goto l_check_dup_error; }

        l_check_dup_error:
        if(unlikely(errno == EINTR)) { // interrupted from where (another process) ?
            goto l_try_again;
        } else { // gg
            print_errno();
            panic("can't restore default file descriptors.");
        }
        status = -2;
    }

    l_close_duplicates:
    if(unlikely((i == 0 && close(in) < 0) || (o == 0 && close(out) < 0) || (e == 0 && close(err) < 0))) {
        print_errno();
        return -2;
    }

    if(unlikely(try_rm_envvars(env) < 0)) 
        return -2;

    return status;
}


ASHE_PRIVATE void reset_signal_handling(void)
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


ASHE_PRIVATE finline int32 try_connect_pipe(PipeContext* ctx)
{
    if(unlikely(dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0
                || dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0
                || (ctx->closefd != -1 && close(ctx->closefd) < 0)))
    {
        print_errno();
        return -1;
    }
    return 0;
}


ASHE_PRIVATE int32 run_cmd(Command* cmd, PipeContext* ctx, Job* job)
{
    pid childPID = fork();

    if(unlikely(childPID == -1)) {
        print_errno();
        return -1;
    } else if(childPID == 0) {
        ArrayCharptr* argva = &cmd->argv;
        ArrayCharptr* enva = &cmd->env;

        ashe.sh_flags.isfork = 1;

        /* If no command were provided, then _exit */
        if(unlikely(argva->len == 0))
            goto l_cleanup;

        /* If we fail adding env vars in fork then just _exit with non-zero
         * status, only in main shell process this is made as recoverable error. */
        if(unlikely(try_add_envvars(enva) < 0))
            goto l_cleanup;

         /* Current process ID */
        pid pid = getpid();

        /* If this is the first process in this job set the gpid as pid
         * and give it the terminal */
        if(job->pgid == 0) {
            job->pgid = pid;
            if(unlikely(job->foreground && tcsetpgrp(STDIN_FILENO, job->pgid) < 0))
                goto l_error;
        }

        /* Move this process into the new process group */
        if(unlikely(setpgid(pid, job->pgid) < 0))
            goto l_error;

        /* This can't fail, all the signals being set to default can be
         * intercepted and handled. Additionally all signal names are valid. */
        reset_signal_handling();

        if(try_connect_pipe(ctx) < 0)
            goto l_cleanup;

        if(try_resolve_redirections(&cmd->fhandles) < 0)
            goto l_cleanup;

        /* If builtin then execute */
        if(is_builtin(ARGV(cmd, 0))) 
            _exit(run_builtin(argva));

        /* Create argv for 'exec' syscall. */
        char** argv = calloc(argva->len + 1, sizeof(char*)); // +1 for NULL as last entry
        memcpy(argv, argva->data, sizeof(char*) * argva->len);
        argv[argva->len] = NULL;

        /* Try exec the non-builtin command */
        if(execvp(argv[0], argv) < 0) {
            aalloc(argv, 0);
            l_error:
            print_errno();
            l_cleanup:
            panic(NULL);
        }
    }

    /* Set the proccess group ID if this is the first
     * process in this job. */
    if(job->pgid == 0) job->pgid = childPID;

    /* Move the process into job's process group.
     * This is also done in the fork to prevent race. */
    if(unlikely(setpgid(childPID, job->pgid) < 0)) {
        print_errno();
        return -1;
    }

    return childPID;
}


ASHE_PRIVATE finline int32 close_pipe(int32* pipe)
{
    if(unlikely(close(pipe[PIPE_R]) < 0 || close(pipe[PIPE_W]) < 0)) {
        print_errno();
        return -1;
    }
    return 0;
}


ASHE_PRIVATE int32 Pipeline_run(Pipeline* pipeline, ubyte bg)
{
    ArrayCommand* commands = &pipeline->commands;
    memmax cmdcnt = commands->len;
    int32 status = 0;
    pid pid;
    Job job;

    const char* input = dupstr(ashe_input());
    Job_init(&job, input, bg);

    uint32 pn = ((cmdcnt - 1) * 2); /* size of pipe array */
    pn = (pn == 0 ? 1 : pn); /* check in case of 0 */
    int32 pipes[pn]; /* pipe storage */

    /// Run the entire pipeline
    for(memmax i = 0; i < cmdcnt; i++) {
        Command* cmd = ArrayCommand_index(commands, i);
        PipeContext ctx;
        Context_init(&ctx);

        if(cmdcnt > 1) { // only a single command ?
            if(job.foreground && (cmd->argv.len == 0 || is_builtin(ARGV(cmd, 0)))) {
                /* In case we don't have a pipeline or a conditional and
                 * only have a single command that is run in foreground and
                 * is a builtin command or only contains environment variables,
                 * then run the command without forking. */
                Job_free(&job);
                return run_cmd_nofork(cmd);
            }
        } else {
            configure_pipe_at(pipes, cmdcnt, i, &ctx);
        }

        /* fork and run the command */
        pid = run_cmd(cmd, &ctx, &job);

        if(unlikely(pid < 0)) {
            Job_free(&job);
            panic(NULL);
        }

        Process proc;
        Process_init(&proc, pid);
        Job_add_process(&job, proc);

        l_close_pipe:
        errno = 0;
        if(unlikely(i != 0 && close_pipe(&pipes[(i - 1) * 2]) < 0))
        {
            if(unlikely(errno == EINTR)) goto l_close_pipe;
            Job_free(&job);
            print_errno();
            panic(NULL);
        }
    }

    if(job.foreground) {
        status = Job_move_to_foreground(&job, 0);
    } else {
        JobControl_add_job(&ashe.sh_jobcntl, &job);
        Job_mark_as_background(&job, 0);
    }

    return status;
}


ASHE_PRIVATE int32 Conditional_run(Conditional* cond)
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
        status = Conditional_run(cond);
    }
    return status;
}
