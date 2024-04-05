#include "abuiltin.h"
#include "ajobcntl.h"
#include "autils.h"
#include "aparser.h"
#include "arun.h"
#include "ashell.h"
#include "aasync.h"

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

ASHE_PRIVATE int32 try_add_envvars(a_arr_ccharp *env)
{
	char *name, *sep, *value;
	memmax len, i;

	len = env->len;
	for (i = 0; i < len; i++) {
		name = dupstr(*a_arr_ccharp_index(env, i));
		sep = strchr(name, '=');
		value = sep + 1;
		*sep = '\0';
		if (unlikely(setenv(name, value, 1) < 0)) {
			ashe_eprintf(runerrors[ERR_VAREXPORT], name);
			ashe_perrno();
			afree(name);
			return -1;
		}
		afree(name);
	}
	return 0;
}

ASHE_PRIVATE int32 try_rm_envvars(a_arr_ccharp *env)
{
	int32 status = 0;
	memmax len, i;
	const char *name;

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

ASHE_PRIVATE int32 try_resolve_redirections(a_arr_redirect *rds, ubyte exec)
{
	ssize fd;
	struct a_redirect *rdp;
	memmax len, badfd, i;

	len = rds->len;
	for (i = 0; i < len; i++) {
		rdp = &fhs->data[i];
		switch (rdp->op) {
		case OP_REDIRECT_ERROUT:
			fd = filepath_is_fd(rdp->filepath);
			if (fd < 0) {
				fd = ashe_wopen(rdp->filepath, rdp->append);
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
			fd = filepath_is_fd(rdp->filepath);
			if (fd < 0) {
				if (rdp->op == OP_REDIRECT_IN)
					fd = ashe_ropen(rdp->filepath);
				else if (rdp->op == OP_REDIRECT_OUT)
					fd = ashe_wopen(rdp->filepath,
							rdp->append);
				else
					fd = ashe_rwopen(rdp->filepath, 0);
				if (fd < 0) {
					ashe_perrno();
					return -1;
				}
			}
			if (!fdisok(rdp->fd)) {
			} else if (redirect_fd_into(rdp->fd, fd) < 0) {
				return -1;
			}
			break;
		case OP_DUP_IN:
		case OP_DUP_OUT:
			if (!fdisok(rdp->fddup)) {
				badfd = rdp->fddup;
				goto l_badfd;
			}
			/* FALLTHRU */
		case OP_CLOSE:
			if (!fdisok(rdp->fd)) {
				badfd = rdp->fd;
				goto l_badfd;
			}
			if (!exec)
				break;
			if (rdp->op == OP_DUP_IN) {
				if (!fdisopened(rdp->fddup, O_RDONLY | O_RDWR)) {
					ashe_eprintf(
						"fd %d is not open for input.");
					return -1;
				}
				goto l_copy_fd;
			} else if (rdp->op == OP_DUP_OUT) {
				if (!fdisopened(rdp->fddup, O_WRONLY | O_RDWR)) {
					ashe_eprintf(
						"fd %d is not open for output.");
					return -1;
				}
l_copy_fd:
				if (unlikely(dup2(rdp->fddup, rdp->fd) < 0)) {
					ashe_perrno();
					return -1;
				}
			} else if (unlikely(close(rdp->fd) < 0)) {
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
	a_arr_ccharp *env = &cmd->env;
	a_arr_ccharp *argv = &cmd->argv;
	a_arr_redirect *rds = &cmd->fhandles;
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
	if (try_resolve_redirections(rds, type == TBI_EXEC) < 0)
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
	a_arr_ccharp *argva = &cmd->argv;
	a_arr_ccharp *enva = &cmd->env;
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

ASHE_PRIVATE int32 Conditional_run(Conditional *cond)
{
	Pipeline *pipeline;
	ArrayCommand *commands;
	PipeContext ctx;
	Command *cmd;
	Process proc;
	uint32 cmdcnt, pipecnt, pn, i, j;
	int32 status, type;
	int32 *pipes = NULL;
	ubyte stopped;
	pid PID;
	Job job;

	pipecnt = cond->pipelines.len;
	for (i = 0; i < pipecnt; i++) {
		pipeline = ArrayPipeline_index(&cond->pipelines, i);
		commands = &pipeline->commands;

		// TODO: Fix this (have pipelines contain input str ?)
		Job_init(&job, dupstrn(cond->input, cond->inlen),
			 cond->is_background);
		printf("Job.input: %p\r\n", job.input);
		ashe_assert(job.input != NULL);

		if ((cmdcnt = commands->len) > 1) {
			pn = ((cmdcnt - 1) * 2);
			pipes = amalloc(sizeof(int32) * pn);
		}

		status = 0;
		for (j = 0; j < cmdcnt; j++) {
			cmd = ArrayCommand_index(commands, j);
			PipeContext_init(&ctx);

			if (cmdcnt > 1) {
				if (unlikely(conf_pipe(pipes, cmdcnt, j, &ctx) <
					     0))
					goto cleanup;
			} else if (job.foreground &&
				   (cmd->argv.len == 0 ||
				    (type = is_builtin(ARGV(cmd, 0))) >= 0)) {
				Job_free(&job);
				return run_cmd_nofork(cmd, type);
			}

			if (unlikely((PID = run_cmd(cmd, &ctx, &job, pipes)) <
				     0))
				goto cleanup;

			ashe_assert(PID > 0);
			Process_init(&proc, PID);
			Job_add_process(&job, proc);

			if (unlikely(j != 0 &&
				     close_pipe(&pipes[(j - 1) * 2]) < 0))
				goto cleanup;
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
		if ((status == 0 && (pipeline->connection & CON_OR)) ||
		    (status != 0 && (pipeline->connection & CON_AND)))
			return status;
	}
	return status;
cleanup:
	Job_free(&job);
	afree(pipes);
	panic(NULL);
	ashe_assertf(0, "unreachable");
	return 0; /* UNREACHED */
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
