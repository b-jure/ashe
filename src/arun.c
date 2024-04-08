#include "aalloc.h"
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

#define PIPE_R 0 /* Read end of a pipe */
#define PIPE_W 1 /* Write end of a pipe */

/* Instructs forked process which pipe stream to dup() and close. */
struct a_pipectx {
	int32 pipefd[2];
	int32 closefd;
};

ASHE_PRIVATE inline void a_pipectx_init(struct a_pipectx *ctx)
{
	ctx->pipefd[0] = STDIN_FILENO;
	ctx->pipefd[1] = STDOUT_FILENO;
	ctx->closefd = -1;
}

ASHE_PRIVATE int32 conf_pipe(int32 *pipes, memmax len, memmax i,
			     struct a_pipectx *ctx)
{
	int32 status;
	int32 *poffset;

	status = 0;
	if (i == 0) {
		poffset = pipes;
		if (unlikely(pipe(poffset) < 0)) {
			defer(-1);
		} else {
			ctx->pipefd[PIPE_W] = poffset[PIPE_W];
			ctx->closefd = poffset[PIPE_R];
		}
	} else if (i != len - 1) {
		poffset = &pipes[i * 2];
		if (unlikely(pipe(poffset) < 0)) {
			defer(-1);
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

defer:
	if (unlikely(status == -1))
		ashe_perrno("can't create pipe");
	return status;
}

ASHE_PRIVATE int32 add_envs(a_arr_ccharp *env)
{
	char *name, *sep, *value;
	memmax len, i;
	int32 status;

	status = 0;
	len = env->len;
	for (i = 0; i < len; i++) {
		name = ashe_dupstr(*a_arr_ccharp_index(env, i));
		sep = strchr(name, '=');
		value = sep + 1;
		*sep = '\0';
		if (unlikely(setenv(name, value, 1) < 0))
			defer(-1);
		*a_arr_ccharp_index(env, i) = name;
		a_arr_ccharp_push(&ashe.sh_buffers, name);
	}

defer:
	if (unlikely(status == -1)) {
		ashe_perrno("setenv");
		afree(name);
	}
	return status;
}

ASHE_PRIVATE int32 rm_envs(a_arr_ccharp *env)
{
	const char *key;
	memmax len, i;
	int32 status;

	status = 0;
	len = env->len;
	for (i = 0; i < len; i++) {
		key = env->data[i];
		/* env is already in format 'key\0value', check add_envs()*/
		if (unlikely(unsetenv(key) < 0))
			status = -1;
	}

	if (unlikely(status != 0))
		ashe_perrno("unsetenv");
	return status;
}

ASHE_PRIVATE inline int32 redirect_errout_into(int32 fd)
{
	if (unlikely(ashe_dup2(fd, STDERR_FILENO) < 0 ||
		     ashe_dup2(fd, STDOUT_FILENO) < 0 || ashe_close(fd) < 0))
		return -1;
	return 0;
}

/* clang-format off */
ASHE_PRIVATE int32 resolve_redirections(a_arr_redirect *rds, ubyte exec)
{
	ssize fd;
	struct a_redirect *rdp;
	memmax len, i;
	ssize badfd;
	int32 status;

	badfd = -1;
	len = rds->len;

	for (i = 0; i < len; i++) {
		rdp = a_arr_redirect_index(rds, i);
		switch (rdp->rd_op) {
		case ARDOP_REDIRECT_CLOB:
			// TODO: Implement
			break;
		case ARDOP_REDIRECT_ERROUT:
			fd = ashe_open(rdp->rd_fname, AHOW_W, rdp->rd_append);
			if (unlikely(fd < 0 || redirect_errout_into(fd) < 0))
				defer(-1);
			break;
		case ARDOP_REDIRECT_INOUT:
			fd = ashe_open(rdp->rd_fname, AHOW_RW, rdp->rd_append);
			goto checkfd;
		case ARDOP_REDIRECT_IN:
			fd = ashe_open(rdp->rd_fname, AHOW_R);
			goto checkfd;
		case ARDOP_REDIRECT_OUT:
			fd = ashe_open(rdp->rd_fname, AHOW_W, rdp->rd_append);
checkfd:
			if (fd < 0)
				defer(-1);
			if (!a_fd_isok(rdp->rd_lhsfd)) {
				badfd = rdp->rd_lhsfd;
				defer(-1);
			}
			if (unlikely(ashe_dup2(fd, rdp->rd_lhsfd) < 0 || ashe_close(fd) < 0))
				defer(-1);
			break;
		case ARDOP_DUP_IN:
		case ARDOP_DUP_OUT:
			if (!a_fd_isok(rdp->rd_rhsfd)) {
				badfd = rdp->rd_rhsfd;
				defer(-1);
			}
			/* FALLTHRU */
		case ARDOP_CLOSE:
			if (!a_fd_isok(rdp->rd_lhsfd)) {
				badfd = rdp->rd_lhsfd;
				defer(-1);
			}
			if (!exec)
				break;
			if (rdp->rd_op == ARDOP_DUP_IN) {
				if (!a_fd_isopen(rdp->rd_rhsfd, O_RDONLY | O_RDWR)) {
					ashe_eprintf("fd %d is not open for input.");
					defer(-1);
				}
				goto copyfd;
			} else if (rdp->rd_op == ARDOP_DUP_OUT) {
				if (!a_fd_isopen(rdp->rd_rhsfd, O_WRONLY | O_RDWR)) {
					ashe_eprintf("fd %d is not open for output.");
					defer(-1);
				}
copyfd:
				if (unlikely(ashe_dup2(rdp->rd_rhsfd, rdp->rd_lhsfd) < 0))
					defer(-1);
			} else if (unlikely(ashe_close(rdp->rd_lhsfd) < 0))
					defer(-1);
			break;
		}
	}
defer:
	if (badfd != -1)
		ashe_eprintf("bad file descriptor %d", badfd);
	return status;
}
/* clang-format on */

/* This runs a built-in command or puts
 * environment variables into 'environ' or both. */
ASHE_PRIVATE int32 run_scmd_nofork(struct a_simple_cmd *scmd,
				   enum a_builtin_type type)
{
	a_arr_ccharp *env = &scmd->sc_env;
	a_arr_ccharp *argv = &scmd->sc_argv;
	a_arr_redirect *rds = &scmd->sc_rds;
	int32 status = 0;
	int32 in = ASHE_FD_0;
	int32 out = ASHE_FD_1;
	int32 err = ASHE_FD_2;

	if (unlikely(add_envs(env) == -1))
		defer(-1);
	if (argv->len == 0)
		defer(0);
	if (unlikely(ashe_dup2(STDIN_FILENO, in) < 0 ||
		     ashe_dup2(STDOUT_FILENO, out) < 0 ||
		     ashe_dup2(STDERR_FILENO, err) < 0)) {
		ashe_panic(NULL);
	}
	if (resolve_redirections(rds, type == TBI_EXEC) < 0)
		status = -1;
	else
		status = ashe_runbin(scmd, type);
	if (unlikely((ashe_dup2(in, STDIN_FILENO)) < 0 ||
		     (ashe_dup2(out, STDOUT_FILENO)) < 0 ||
		     (ashe_dup2(err, STDERR_FILENO)) < 0)) {
		ashe_panic("can't restore file descriptors.");
	}
	if (unlikely(ashe_close(in) < 0 || ashe_close(out) < 0 ||
		     ashe_close(err) < 0))
		return -1;
	if (unlikely(rm_envs(env) < 0))
		return -1;
defer:
	return status;
}

ASHE_PRIVATE int32 reset_signal_handling(void)
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
		ashe_perrno("can't reset signal handlers");
		return -1;
	}
	ashe_mask_signals(SIG_UNBLOCK);
	return 0;
}

ASHE_PRIVATE inline int32 try_connect_pipe(struct a_pipectx *ctx)
{
	if (unlikely(ashe_dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0 ||
		     ashe_dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0 ||
		     (ctx->closefd != -1 && ashe_close(ctx->closefd) < 0)))
		return -1;
	return 0;
}

/* 'pipes' are passed just to cleanup them inside of fork */
ASHE_PRIVATE int32 run_scmd_fork(struct a_simple_cmd *scmd,
				 struct a_pipectx *ctx, struct a_job *job,
				 int32 *pipes)
{
	a_arr_ccharp *aargv = &scmd->sc_argv;
	a_arr_ccharp *aenv = &scmd->sc_env;
	char **argv;
	uint32 argc;
	int32 type;
	pid PID;

	PID = fork();

	if (unlikely(PID < 0)) {
		ashe_perrno("fork failed");
		return -1;
	} else if (PID != 0) {
		if (job->pgid == 0)
			job->pgid = PID;
		/* This is also done in the fork to prevent race. */
		if (unlikely(setpgid(PID, job->pgid) < 0)) {
			ashe_perrno("failed setting process group ID (%d)",
				    job->pgid);
			return -1;
		}
		return PID;
	}
	/* fork */
	ashe.sh_flags.isfork = 1;
	argc = a_arr_ccharp_len(aargv);

	if (unlikely(argc == 0 || add_envs(aenv) < 0))
		defer_no_status();

	PID = getpid();

	if (job->pgid == 0) {
		job->pgid = PID;
		if (unlikely(job->foreground &&
			     tcsetpgrp(STDIN_FILENO, job->pgid) < 0)) {
			ashe_perrno("tcsetpgrp(%d, %d) failed", STDIN_FILENO,
				    job->pgid);
			defer_no_status();
		}
	}

	if (unlikely(setpgid(PID, job->pgid) < 0)) {
		ashe_perrno("failed setting process group ID (%d)", job->pgid);
		defer_no_status();
	}
	if (unlikely(reset_signal_handling() < 0))
		defer_no_status();

	type = ashe_isbin(ARGV(scmd, 0));

	if (try_connect_pipe(ctx) < 0 ||
	    resolve_redirections(&scmd->sc_rds, type == TBI_EXEC) < 0)
		defer_no_status();

	if (type != -1) {
		if (pipes)
			afree(pipes);
		a_job_free(job);
		_exit(ashe_runbin(scmd, type));
	}

	argv = acalloc(argc + 1, sizeof(char *));
	memcpy(argv, scmd->sc_argv.data, sizeof(char *) * argc);
	argv[aargv->len] = NULL;

	if (execvp(argv[0], argv) < 0) {
		if (errno == ENOENT)
			ashe_eprintf("unknown command '%s'", argv[0]);
		else
			ashe_perrno("execvp");
		afree(argv);
defer:
		if (pipes != NULL)
			afree(pipes);
		a_job_free(job);
		ashe_exit(EXIT_FAILURE);
	}
	/* UNREACHED */
	ashe_assert(0);
	return 0;
}

ASHE_PRIVATE inline int32 close_pipe(int32 *pipe)
{
	if (unlikely(ashe_close(pipe[PIPE_R]) < 0 ||
		     ashe_close(pipe[PIPE_W]) < 0))
		return -1;
	return 0;
}

ASHE_PRIVATE int32 a_run_simple_cmd(struct a_simple_cmd *scmd,
				    struct a_job *job, uint32 i, int32 *pipes,
				    uint32 cmdcnt)
{
	struct a_process proc;
	struct a_pipectx ctx;
	int32 type, status;
	pid PID;

	status = 1;
	a_pipectx_init(&ctx);

	if (cmdcnt > 1) {
		ashe_assert(pipes != NULL);
		if (unlikely(conf_pipe(pipes, cmdcnt, i, &ctx) < 0))
			defer(-1);
	} else if (job->foreground &&
		   (ARGC(scmd) == 0 ||
		    (type = ashe_isbin(ARGV(scmd, 0))) >= 0)) {
		a_job_free(job);
		return run_scmd_nofork(scmd, type);
	}

	if (unlikely((PID = run_scmd_fork(scmd, &ctx, job, pipes)) < 0))
		defer(-1);
	a_process_init(&proc, PID);
	a_job_add_process(job, proc);
	if (unlikely(i != 0 && close_pipe(&pipes[(i - 1) * 2]) < 0))
		defer(-1);
defer:
	if (unlikely(status < 0)) {
		a_job_free(job);
		if (pipes)
			afree(pipes);
		ashe_panic(NULL);
	}
	/* UNREACHED */
	return status;
}

ASHE_PRIVATE int32 a_run_cmd(struct a_cmd *cmd, struct a_job *job, uint32 i,
			     int32 *pipes, uint32 cmdcnt)
{
	ashe_assert(cmd != NULL);
	ashe_assert(job != NULL);

	switch (cmd->c_type) {
	case ACMD_SIMPLE:
		return a_run_simple_cmd(&cmd->c_u.scmd, job, i, pipes, cmdcnt);
	default: /* UNREACHED */
		ashe_assert(0);
	}
}

ASHE_PRIVATE int32 a_run_pipeline(struct a_pipeline *restrict pipeline)
{
	a_arr_cmd *cmds;
	struct a_cmd *cmd;
	struct a_job job;
	int32 *pipes;
	int32 status;
	uint32 cmdcnt, pn, i;
	ubyte stopped;

	cmds = &pipeline->pl_cmds;
	a_job_init(&job, pipeline->pl_input, pipeline->pl_bg);

	ashe_assert(job.foreground == !pipeline->pl_bg);
	ashe_assert(job.input != NULL);

	if ((cmdcnt = a_arr_cmd_len(cmds)) > 1) {
		pn = ((cmdcnt - 1) * 2);
		pipes = amalloc(sizeof(int32) * pn);
	} else {
		pipes = NULL;
	}

	ashe_assert(cmdcnt >= 1);

	for (i = 0; i < cmdcnt; i++) {
		cmd = a_arr_cmd_index(cmds, i);
		status = a_run_cmd(cmd, &job, i, pipes, cmdcnt);
		if (status >= 1) { /* forked ? */
			status = 0;
		} else { /* otherwise it must be builtin command */
			ashe_assert(i == 0 && cmdcnt == 1);
			return status;
		}
	}

	if (job.foreground) {
		status = a_job_move_to_foreground(&job, 0, &stopped);
		if (!stopped) /* job done ? */
			a_job_free(&job);
	} else {
		a_jobcntl_add_job(&ashe.sh_jobcntl, &job);
		a_job_mark_as_background(&job, 0);
	}

	if (cmdcnt > 1)
		afree(pipes);
	return status;
}

ASHE_PRIVATE int32 a_run_list(struct a_list *restrict list)
{
	a_arr_pipeline *pipes;
	struct a_pipeline *pipeline;
	int32 status;
	uint32 i;

	status = 0;
	pipes = &list->ls_pipes;
	for (i = 0; i < pipes->len; i++) {
		pipeline = a_arr_pipeline_index(pipes, i);
		status = a_run_pipeline(pipeline);
		if (pipeline->pl_con != ACON_NONE &&
		    ((status == 0 && pipeline->pl_con == ACON_OR) ||
		     (status != 0 && pipeline->pl_con == ACON_AND)))
			return status;
	}
	return status;
}

ASHE_PUBLIC int32 a_run_block(struct a_block *restrict block)
{
	memmax i;
	uint32 listcnt;
	int32 status;
	struct a_list *list;

	status = 0;
	listcnt = block->bl_lists.len;
	for (i = 0; i < listcnt; i++) {
		list = a_arr_list_index(&block->bl_lists, i);
		status = a_run_list(list);
	}
	return status;
}
