#include "abuiltin.h"
#include "ajobcntl.h"
#include "autils.h"
#include "aparser.h"
#include "arun.h"
#include "ashell.h"
#include "aasync.h"

#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#define DEV_DIR "/dev/"
ASHE_PRIVATE const char *devfiles[] = {
	DEV_DIR "stdin",
	DEV_DIR "stdout",
	DEV_DIR "stderr",
};

#define PIPE_R 0 /* Read end of a pipe */
#define PIPE_W 1 /* Write end of a pipe */

/* open file for writing '>' or '>>' */
#define ashe_wopen(file, append) \
	open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_WRONLY, 0666)

/* open file for reading '<' */
#define ashe_ropen(file) open(file, O_RDONLY)

/* open file for reading and writing '<>' */
#define ashe_rwopen(file, append) \
	open(file, O_CREAT | ((append) ? O_APPEND : O_TRUNC) | O_RDWR, 0666)

#define ERR_VAREXPORT 0
#define ERR_VARRM     1
#define ERR_BADFD     2
ASHE_PRIVATE const char *runerrors[] = {
	"failed exporting variable '%s'.",
	"couldn't remove variable '%s'.",
	"bad file descriptor '%zu'.",
};

/* Instructs forked process which pipe stream to dup() and close. */
typedef struct {
	int32 pipefd[2];
	int32 closefd;
} PipeContext;

ASHE_PRIVATE inline void PipeContext_init(PipeContext *ctx)
{
	ctx->pipefd[0] = STDIN_FILENO;
	ctx->pipefd[1] = STDOUT_FILENO;
	ctx->closefd = -1;
}

ASHE_PRIVATE int32 conf_pipe(int32 *pipes, memmax len, memmax i,
			     PipeContext *ctx)
{
	int32 *poffset;

	if (i == 0) {
		poffset = pipes;
		if (unlikely(pipe(poffset) < 0)) {
			ashe_perrno();
			return -1;
		} else {
			ctx->pipefd[PIPE_W] = poffset[PIPE_W];
			ctx->closefd = poffset[PIPE_R];
		}
	} else if (i != len - 1) {
		poffset = &pipes[i * 2];
		if (unlikely(pipe(poffset) < 0)) {
			ashe_perrno();
			return -1;
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
	return 0;
}

ASHE_PRIVATE int32 try_add_envvars(ArrayCharptr *env)
{
	char *name, *sep, *value;
	memmax len, i;

	len = env->len;
	for (i = 0; i < len; i++) {
		name = env->data[i];
		sep = strchr(name, '=');
		value = sep + 1;
		*sep = '\0';
		if (unlikely(setenv(name, value, 1) < 0)) {
			ashe_eprintf(runerrors[ERR_VAREXPORT], name);
			ashe_perrno();
			return -1;
		}
	}
	return 0;
}

ASHE_PRIVATE int32 try_rm_envvars(ArrayCharptr *env)
{
	int32 status = 0;
	memmax len, i;
	char *name;

	len = env->len;
	for (i = 0; i < len; i++) {
		name = env->data[i];
		// name is already in format 'key\0value'
		if (unlikely(unsetenv(name) < 0)) { // can this ever fail ?
			ashe_eprintf(runerrors[ERR_VARRM], name);
			ashe_perrno();
			status = -1;
			continue; // try remove the rest
		}
	}
	return status;
}

ASHE_PRIVATE inline int32 filepath_is_fd(const char *filepath)
{
	memmax i;

	for (i = 0; i < ELEMENTS(devfiles); i++) {
		if (strcmp(filepath, devfiles[i]) != 0)
			continue;
		if (fdisvalid(i))
			return i;
		break;
	}
	return -1;
}

ASHE_PRIVATE inline int32 redirect_stderrout_into(int32 fd)
{
	if ((dup2(STDOUT_FILENO, STDERR_FILENO) == 0 &&
	     dup2(fd, STDOUT_FILENO) == 0)) {
		ashe_perrno();
		return -1;
	}
	return 0;
}

ASHE_PRIVATE inline int32 redirect_fd_into(int32 fd, int32 to)
{
	if (dup2(to, fd) < 0) {
		ashe_perrno();
		return -1;
	}
	return 0;
}

ASHE_PRIVATE int32 try_resolve_redirections(ArrayFileHandle *fhs, ubyte exec)
{
	ssize fd;
	FileHandle *fh;
	memmax len, badfd, i;

	len = fhs->len;
	for (i = 0; i < len; i++) {
		fh = &fhs->data[i];
		switch (fh->op) {
		case OP_REDIRECT_ERROUT:
			fd = filepath_is_fd(fh->filepath);
			if (fd < 0) {
				fd = ashe_wopen(fh->filepath, fh->append);
				if (fd < 0) {
					ashe_perrno();
					return -1;
				}
			}
			if (unlikely(!fdisok(fd))) {
				badfd = fd;
				goto l_badfd;
			} else if (redirect_stderrout_into(fd) < 0) {
				return -1;
			}
			break;
		case OP_REDIRECT_INOUT:
		case OP_REDIRECT_IN:
		case OP_REDIRECT_OUT:
			fd = filepath_is_fd(fh->filepath);
			if (fd < 0) {
				if (fh->op == OP_REDIRECT_IN)
					fd = ashe_ropen(fh->filepath);
				else if (fh->op == OP_REDIRECT_OUT)
					fd = ashe_wopen(fh->filepath,
							fh->append);
				else
					fd = ashe_rwopen(fh->filepath, 0);
				if (fd < 0) {
					ashe_perrno();
					return -1;
				}
			}
			if (!fdisok(fh->fd)) {
			} else if (redirect_fd_into(fh->fd, fd) < 0) {
				return -1;
			}
			break;
		case OP_DUP_IN:
		case OP_DUP_OUT:
			if (!fdisok(fh->fddup)) {
				badfd = fh->fddup;
				goto l_badfd;
			}
			/* FALLTHRU */
		case OP_CLOSE:
			if (!fdisok(fh->fd)) {
				badfd = fh->fd;
				goto l_badfd;
			}
			if (!exec)
				break;
			if (fh->op == OP_DUP_IN) {
				if (!fdisopened(fh->fddup, O_RDONLY | O_RDWR)) {
					ashe_eprintf(
						"fd %d is not open for input.");
					return -1;
				}
				goto l_copy_fd;
			} else if (fh->op == OP_DUP_OUT) {
				if (!fdisopened(fh->fddup, O_WRONLY | O_RDWR)) {
					ashe_eprintf(
						"fd %d is not open for output.");
					return -1;
				}
l_copy_fd:
				if (unlikely(dup2(fh->fddup, fh->fd) < 0)) {
					ashe_perrno();
					return -1;
				}
			} else if (unlikely(close(fh->fd) < 0)) {
				ashe_perrno();
				return -1;
			}
			break;
		}
	}
	return 0;

l_badfd:
	ashe_eprintf(runerrors[ERR_BADFD], badfd);
	return -1;
}

/* This runs a built-in command or puts environment
 * variables into 'environ' or both. */
ASHE_PRIVATE int32 run_cmd_nofork(Command *cmd, enum tbi type)
{
	ArrayCharptr *env = &cmd->env;
	ArrayCharptr *argv = &cmd->argv;
	ArrayFileHandle *fhs = &cmd->fhandles;
	int32 status = 0;
	int32 in = ASHE_FD_0;
	int32 out = ASHE_FD_1;
	int32 err = ASHE_FD_2;

	if (unlikely(try_add_envvars(env) == -1))
		return -2;
	if (argv->len == 0)
		return 0;
	if (unlikely(dup2(STDIN_FILENO, in) < 0 ||
		     dup2(STDOUT_FILENO, out) < 0 ||
		     dup2(STDERR_FILENO, err) < 0)) {
		goto error;
	}
	if (try_resolve_redirections(fhs, type == TBI_EXEC) < 0)
		status = -2;
	else
		status = run_builtin(cmd, type);
	if (unlikely((dup2(in, STDIN_FILENO)) < 0 ||
		     (dup2(out, STDOUT_FILENO)) < 0 ||
		     (dup2(err, STDERR_FILENO)) < 0)) {
		ashe_perrno();
		panic("can't restore default file descriptors.");
	}
	if (unlikely(close(in) < 0 || close(out) < 0 || close(err) < 0)) {
		ashe_perrno();
		return -2;
	}
	if (unlikely(try_rm_envvars(env) < 0))
		return -2;
	return status;
error:
	ashe_perrno();
	panic(NULL);
	return 0; /* UNREACHED */
}

ASHE_PRIVATE void reset_signal_handling(void)
{
	struct sigaction sigdfl_ac;

	sigemptyset(&sigdfl_ac.sa_mask);
	sigdfl_ac.sa_flags = 0;
	sigdfl_ac.sa_handler = SIG_DFL;
	if (unlikely(sigaction(SIGINT, &sigdfl_ac, NULL) < 0 ||
		     sigaction(SIGCHLD, &sigdfl_ac, NULL) < 0 ||
		     sigaction(SIGWINCH, &sigdfl_ac, NULL) < 0 ||
		     sigaction(SIGQUIT, &sigdfl_ac, NULL) < 0 ||
		     sigaction(SIGTSTP, &sigdfl_ac, NULL) < 0 ||
		     sigaction(SIGTTIN, &sigdfl_ac, NULL) < 0 ||
		     sigaction(SIGTTOU, &sigdfl_ac, NULL) < 0)) {
		ashe_perrno();
		panic(NULL);
	}
	ashe_mask_signals(SIG_UNBLOCK);
}

ASHE_PRIVATE inline int32 try_connect_pipe(PipeContext *ctx)
{
	if (unlikely(dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0 ||
		     dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0 ||
		     (ctx->closefd != -1 && close(ctx->closefd) < 0))) {
		ashe_perrno();
		return -1;
	}
	return 0;
}

/* 'pipes' are passed just to cleanup them inside of fork */
ASHE_PRIVATE int32 run_cmd(Command *cmd, PipeContext *ctx, Job *job,
			   int32 *pipes)
{
	ArrayCharptr *argva = &cmd->argv;
	ArrayCharptr *enva = &cmd->env;
	char **argv;
	int32 type;
	pid PID = fork();

	if (unlikely(PID < 0)) {
		ashe_perrno();
		return -1;
	} else if (PID != 0) {
		if (job->pgid == 0)
			job->pgid = PID;
		/* This is also done in the fork to prevent race. */
		if (unlikely(setpgid(PID, job->pgid) < 0)) {
			ashe_perrno();
			return -1;
		}
		return PID;
	}
	/* forked process */
	ashe_assert(PID == 0);
	ashe.sh_flags.isfork = 1;
	afree(pipes);

	if (unlikely(argva->len == 0 || try_add_envvars(enva) < 0))
		goto cleanup;

	PID = getpid();
	if (job->pgid == 0) {
		job->pgid = PID;
		if (unlikely(job->foreground &&
			     tcsetpgrp(STDIN_FILENO, job->pgid) < 0))
			goto error;
	}

	if (unlikely(setpgid(PID, job->pgid) < 0))
		goto error;

	Job_free(job);
	reset_signal_handling();

	type = is_builtin(ARGV(cmd, 0));
	if (try_connect_pipe(ctx) < 0 ||
	    try_resolve_redirections(&cmd->fhandles, type == TBI_EXEC) < 0)
		goto panic;
	if (type != -1)
		_exit(run_builtin(cmd, type));

	argv = acalloc(argva->len + 1, sizeof(char *));
	memcpy(argv, argva->data, sizeof(char *) * argva->len);
	argv[argva->len] = NULL;

	if (execvp(argv[0], argv) < 0) {
		afree(argv);
error:
		ashe_perrno();
cleanup:
		Job_free(job);
panic:
		panic(NULL);
	}
	ashe_assertf(0, "unreachable");
	return 0; /* UNREACHED */
}

ASHE_PRIVATE inline int32 close_pipe(int32 *pipe)
{
	if (unlikely(close(pipe[PIPE_R]) < 0 || close(pipe[PIPE_W]) < 0)) {
		ashe_perrno();
		return -1;
	}
	return 0;
}

ASHE_PRIVATE int32 Pipeline_run(Pipeline *pipeline, ubyte bg)
{
	ArrayCommand *commands = &pipeline->commands;
	int32 *pipes = NULL;
	uint32 cmdcnt, pn, i;
	int32 status, type;
	ubyte stopped;
	PipeContext ctx;
	Command *cmd;
	Process proc;
	pid PID;
	Job job;

	Job_init(&job, dupstrn(IBF.data, IBF.len), bg);
	ashe_assert(job.input != NULL);

	if ((cmdcnt = commands->len) > 1) {
		pn = ((cmdcnt - 1) * 2);
		pipes = amalloc(sizeof(int32) * pn);
	}

	status = 0;
	for (i = 0; i < cmdcnt; i++) {
		cmd = ArrayCommand_index(commands, i);
		PipeContext_init(&ctx);

		if (cmdcnt > 1) {
			if (unlikely(conf_pipe(pipes, cmdcnt, i, &ctx) < 0))
				goto cleanup;
		} else if (job.foreground &&
			   (cmd->argv.len == 0 ||
			    (type = is_builtin(ARGV(cmd, 0))) >= 0)) {
			Job_free(&job);
			return run_cmd_nofork(cmd, type);
		}

		if (unlikely((PID = run_cmd(cmd, &ctx, &job, pipes)) < 0))
			goto cleanup;

		ashe_assert(PID > 0);
		Process_init(&proc, PID);
		Job_add_process(&job, proc);
l_close_pipe:
		errno = 0;
		if (unlikely(i != 0 && close_pipe(&pipes[(i - 1) * 2]) < 0)) {
			if (unlikely(errno == EINTR))
				goto l_close_pipe;
			goto cleanup;
		}
	}

	if (job.foreground) {
		status = Job_move_to_foreground(&job, 0, &stopped);
		if (!stopped) /* job done ? */
			Job_free(&job);
	} else {
		JobControl_add_job(&ashe.sh_jobcntl, &job);
		Job_mark_as_background(&job, 0);
	}

	if (cmdcnt > 1) {
		ashe_assert(pipes != NULL);
		afree(pipes);
	}
	return status;
cleanup:
	Job_free(&job);
	afree(pipes);
	panic(NULL);
	ashe_assertf(0, "unreachable");
	return 0; /* UNREACHED */
}

ASHE_PRIVATE int32 Conditional_run(Conditional *cond)
{
	ArrayPipeline *pipes = &cond->pipelines;
	Pipeline *pipeline;
	int32 status = 0;
	memmax i;

	for (i = 0; i < pipes->len; i++) {
		pipeline = ArrayPipeline_index(pipes, i);
		status = Pipeline_run(pipeline, cond->is_background);
		if ((status == 0 && (pipeline->connection & CON_OR)) ||
		    (status != 0 && (pipeline->connection & CON_AND)))
			return status;
	}
	return status;
}

ASHE_PUBLIC int32 cmdexec(ArrayConditional *conds)
{
	memmax i;
	int32 status = 0;
	Conditional *cond;

	for (i = 0; i < conds->len; i++) {
		cond = ArrayConditional_index(conds, i);
		status = Conditional_run(cond);
	}
	return status;
}
