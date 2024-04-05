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
		if (fd_isvalid(i))
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
		rdp = a_arr_redirect_index(rds, i);
		switch (rdp->rd_op) {
		case ARDOP_REDIRECT_CLOB:
			// TODO: Implement
			break;
		case ARDOP_REDIRECT_ERROUT:
			fd = filepath_is_fd(rdp->rd_fname);
			if (fd < 0) {
				fd = ashe_wopen(rdp->rd_fname, rdp->rd_append);
				if (fd < 0) {
					ashe_perrno();
					return -1;
				}
			}
			if (unlikely(!fd_isok(fd))) {
				badfd = fd;
				goto l_badfd;
			} else if (redirect_stderrout_into(fd) < 0) {
				return -1;
			}
			break;
		case ARDOP_REDIRECT_INOUT:
		case ARDOP_REDIRECT_IN:
		case ARDOP_REDIRECT_OUT:
			fd = filepath_is_fd(rdp->rd_fname);
			if (fd < 0) {
				if (rdp->rd_op == ARDOP_REDIRECT_IN)
					fd = ashe_ropen(rdp->rd_fname);
				else if (rdp->rd_op == ARDOP_REDIRECT_OUT)
					fd = ashe_wopen(rdp->rd_fname,
							rdp->rd_append);
				else
					fd = ashe_rwopen(rdp->rd_fname, 0);
				if (fd < 0) {
					ashe_perrno();
					return -1;
				}
			}
			if (!fd_isok(rdp->rd_lhsfd)) {
			} else if (redirect_fd_into(rdp->rd_lhsfd, fd) < 0) {
				return -1;
			}
			break;
		case ARDOP_DUP_IN:
		case ARDOP_DUP_OUT:
			if (!fd_isok(rdp->rd_rhsfd)) {
				badfd = rdp->rd_rhsfd;
				goto l_badfd;
			}
			/* FALLTHRU */
		case ARDOP_CLOSE:
			if (!fd_isok(rdp->rd_lhsfd)) {
				badfd = rdp->rd_lhsfd;
				goto l_badfd;
			}
			if (!exec)
				break;
			if (rdp->rd_op == ARDOP_DUP_IN) {
				if (!fd_isopen(rdp->rd_rhsfd,
					       O_RDONLY | O_RDWR)) {
					ashe_eprintf(
						"fd %d is not open for input.");
					return -1;
				}
				goto l_copy_fd;
			} else if (rdp->rd_op == ARDOP_DUP_OUT) {
				if (!fd_isopen(rdp->rd_rhsfd,
					       O_WRONLY | O_RDWR)) {
					ashe_eprintf(
						"fd %d is not open for output.");
					return -1;
				}
l_copy_fd:
				if (unlikely(dup2(rdp->rd_rhsfd,
						  rdp->rd_lhsfd) < 0)) {
					ashe_perrno();
					return -1;
				}
			} else if (unlikely(close(rdp->rd_lhsfd) < 0)) {
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
		status = run_builtin(scmd, type);
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

ASHE_PRIVATE inline int32 try_connect_pipe(struct a_pipectx *ctx)
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
ASHE_PRIVATE int32 run_scmd_fork(struct a_simple_cmd *scmd,
				 struct a_pipectx *ctx, struct a_job *job,
				 int32 *pipes)
{
	a_arr_ccharp *argva = &scmd->sc_argv;
	a_arr_ccharp *enva = &scmd->sc_env;
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

	a_job_free(job);
	reset_signal_handling();

	type = is_builtin(*a_arr_ccharp_index(&scmd->sc_argv, 0));
	if (try_connect_pipe(ctx) < 0 ||
	    try_resolve_redirections(&scmd->sc_rds, type == TBI_EXEC) < 0)
		goto panic;
	if (type != -1)
		_exit(run_builtin(scmd, type));

	argv = acalloc(argva->len + 1, sizeof(char *));
	memcpy(argv, argva->data, sizeof(char *) * argva->len);
	argv[argva->len] = NULL;

	if (execvp(argv[0], argv) < 0) {
		afree(argv);
error:
		ashe_perrno();
cleanup:
		a_job_free(job);
panic:
		panic(NULL);
	}
	ashe_assert(0);
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

ASHE_PRIVATE int32 a_run_simple_cmd(struct a_simple_cmd *scmd,
				    struct a_job *job, uint32 i, int32 *pipes,
				    uint32 cmdcnt)
{
	struct a_process proc;
	struct a_pipectx ctx;
	int32 type;
	pid PID;

	a_pipectx_init(&ctx);

	if (cmdcnt > 1) {
		if (unlikely(conf_pipe(pipes, cmdcnt, i, &ctx) < 0))
			goto cleanup;
	} else if (job->foreground && (a_arr_ccharp_len(&scmd->sc_argv) == 0 ||
				       (type = is_builtin(*a_arr_ccharp_index(
						&scmd->sc_argv, 0))))) {
		a_job_free(job);
		return run_scmd_nofork(scmd, type);
	}

	if (unlikely((PID = run_scmd_fork(scmd, &ctx, job, pipes)) < 0))
		goto cleanup;
	a_process_init(&proc, PID);
	a_job_add_process(job, proc);
	if (unlikely(i != 0 && close_pipe(&pipes[(i - 1) * 2]) < 0))
		goto cleanup;

	return 1; /* indicate we forked */
cleanup:
	a_job_free(job);
	afree(pipes);
	panic(NULL);
	/* UNREACHED */
	return 0;
}

ASHE_PRIVATE int32 a_run_cmd(struct a_cmd *cmd, struct a_job *job, uint32 i,
			     int32 *pipes, uint32 cmdcnt)
{
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
	ashe_assert(job.input != NULL);

	if ((cmdcnt = a_arr_cmd_len(cmds)) > 1) {
		pn = ((cmdcnt - 1) * 2);
		pipes = amalloc(sizeof(int32) * pn);
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
