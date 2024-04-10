#include "aalloc.h"
#include "abuiltin.h"
#include "acommon.h"
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

#define N_OR(n, dflt) ((n) == -1 ? (dflt) : (n))

#define reset_dirtyfd() memset(ashe.sh_dirtyfd, 0, sizeof(ashe.sh_dirtyfd))

/* Instructs forked process which pipe stream to dup() and close. */
struct a_pipectx {
	a_int32 pipefd[2];
	a_int32 closefd;
};

ASHE_PRIVATE inline void a_pipectx_init(struct a_pipectx *ctx)
{
	ctx->pipefd[0] = STDIN_FILENO;
	ctx->pipefd[1] = STDOUT_FILENO;
	ctx->closefd = -1;
}

ASHE_PRIVATE a_int32 conf_pipe(a_int32 *pipes, a_memmax len, a_memmax i,
			       struct a_pipectx *ctx)
{
	a_int32 status;
	a_int32 *poffset;

	status = 0;
	if (i == 0) {
		poffset = pipes;
		if (ASHE_UNLIKELY(pipe(poffset) < 0)) {
			ASHE_DEFER(-1);
		} else {
			ctx->pipefd[PIPE_W] = poffset[PIPE_W];
			ctx->closefd = poffset[PIPE_R];
		}
	} else if (i != len - 1) {
		poffset = &pipes[i * 2];
		if (ASHE_UNLIKELY(pipe(poffset) < 0)) {
			ASHE_DEFER(-1);
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
	if (ASHE_UNLIKELY(status == -1))
		ashe_perrno("can't create pipe");
	return status;
}

ASHE_PRIVATE a_int32 add_envs(a_arr_ccharp *env)
{
	char *name, *sep, *value;
	a_memmax len, i;
	a_int32 status;

	status = 0;
	len = env->len;
	for (i = 0; i < len; i++) {
		name = ashe_dupstr(*a_arr_ccharp_index(env, i));
		sep = strchr(name, '=');
		value = sep + 1;
		*sep = '\0';
		if (ASHE_UNLIKELY(setenv(name, value, 1) < 0))
			ASHE_DEFER(-1);
		*a_arr_ccharp_index(env, i) = name;
		a_arr_ccharp_push(&ashe.sh_buffers, name);
	}

defer:
	if (ASHE_UNLIKELY(status == -1)) {
		ashe_perrno("setenv");
		afree(name);
	}
	return status;
}

ASHE_PRIVATE a_int32 rm_envs(a_arr_ccharp *env)
{
	const char *key;
	a_memmax len, i;
	a_int32 status;

	status = 0;
	len = env->len;
	for (i = 0; i < len; i++) {
		key = env->data[i];
		/* env is already in format 'key\0value', check add_envs()*/
		if (ASHE_UNLIKELY(unsetenv(key) < 0))
			status = -1;
	}

	if (ASHE_UNLIKELY(status != 0))
		ashe_perrno("unsetenv");
	return status;
}

ASHE_PRIVATE inline a_int32 redirect_op(a_int32 oldfd, a_int32 newfd)
{
	if (ASHE_UNLIKELY(ashe_dup2(oldfd, newfd) < 0 || ashe_close(oldfd) < 0))
		return -1;
	return 0;
}

ASHE_PRIVATE inline a_int32 redirect_op_errout(a_int32 fd)
{
	if (ASHE_UNLIKELY(ashe_dup2(fd, STDERR_FILENO) < 0 ||
			  ashe_dup2(fd, STDOUT_FILENO) < 0 || ashe_close(fd) < 0))
		return -1;
	return 0;
}

ASHE_PRIVATE inline void set_if_dirty(a_int32 fd)
{
	switch (fd) {
	case STDIN_FILENO:
		ashe.sh_dirtyfd[STDIN_FILENO] = 1;
		break;
	case STDOUT_FILENO:
		ashe.sh_dirtyfd[STDOUT_FILENO] = 1;
		break;
	case STDERR_FILENO:
		ashe.sh_dirtyfd[STDERR_FILENO] = 1;
		break;
	default:
		break;
	}
}

ASHE_PRIVATE inline a_int32 fd_assert_bounds(a_ssize fd)
{
	if (fd < 0 || fd > INT_MAX) {
		ashe_eprintf("file descriptor %zd out of bounds.", fd);
		return -1;
	}
	return 0;
}

ASHE_PRIVATE inline a_int32 fd_assert_perms(a_int32 fd, a_memmax perms)
{
	if (!a_fd_isopen(fd, perms)) {
		ashe_eprintf("file descriptor %d lacks required permissions.", fd);
		return -1;
	}
	return 0;
}

ASHE_PRIVATE inline a_int32 fd_assert_valid(a_int32 fd)
{
	errno = 0;
	if (fcntl(fd, F_GETFD) < 0 || errno == EBADFD) {
		ashe_perrno("bad file descriptor %d");
		return -1;
	}
	return 0;
}

ASHE_PRIVATE a_int32 resolve_redirections(a_arr_redirect *rds, a_ubyte exec)
{
	a_ssize fd;
	struct a_redirect *rdp;
	a_memmax len, i, perms;
	a_int32 status;
	a_ubyte how;

	len = rds->len;

	for (i = 0; i < len; i++) {
		rdp = a_arr_redirect_index(rds, i);
		switch (rdp->rd_op) {
		case ARDOP_REDIRECT_CLOB:
			// TODO: Implement
			break;
		case ARDOP_REDIRECT_ERROUT:
			ashe_assert(rdp->rd_lhsfd == -1);
			ashe_assert(rdp->rd_rhsfd == -1);
			ashe_assert(rdp->rd_fname);

			fd = ashe_open(rdp->rd_fname, AHOW_W, rdp->rd_append);
			if (fd < 0 || ASHE_UNLIKELY(redirect_op_errout(fd) < 0))
				ASHE_DEFER(-1);
			break;
		case ARDOP_REDIRECT_INOUT:
			ashe_assert(rdp->rd_append == 0);
			ashe_assert(rdp->rd_rhsfd == -1);
			ashe_assert(rdp->rd_lhsfd != -1);
			ashe_assert(rdp->rd_fname);

			fd = ashe_open(rdp->rd_fname, AHOW_RW, rdp->rd_append);
			if (fd < 0 || fd_assert_bounds(rdp->rd_lhsfd) < 0)
				ASHE_DEFER(-1);
			if (!exec) {
				if (ASHE_UNLIKELY(ashe_close(fd) < 0))
					ASHE_DEFER(-1);
				break;
			}
			if (fd_assert_valid(rdp->rd_lhsfd) < 0 ||
			    ASHE_UNLIKELY(redirect_op(fd, rdp->rd_lhsfd) < 0)) {
				ashe_close(fd);
				ASHE_DEFER(-1);
			}
			set_if_dirty(rdp->rd_lhsfd);
			break;
		case ARDOP_REDIRECT_IN:
			ashe_assert(rdp->rd_append == 0);

			how = AHOW_R;
			goto redirect;
		case ARDOP_REDIRECT_OUT:
			how = AHOW_W;
redirect:
			ashe_assert(rdp->rd_rhsfd == -1);
			ashe_assert(rdp->rd_lhsfd != -1);
			ashe_assert(rdp->rd_fname);

			fd = ashe_open(rdp->rd_fname, how, rdp->rd_append);
			if (fd < 0 || fd_assert_bounds(rdp->rd_lhsfd) < 0)
				ASHE_DEFER(-1);
			if (ASHE_UNLIKELY(redirect_op(fd, rdp->rd_lhsfd) < 0))
				ASHE_DEFER(-1);
			break;
		case ARDOP_DUP_IN:
		case ARDOP_DUP_OUT:
			ashe_assert(rdp->rd_lhsfd != -1);
			ashe_assert(rdp->rd_rhsfd != -1);
			ashe_assert(rdp->rd_fname == NULL);

			if (fd_assert_bounds(rdp->rd_rhsfd) < 0)
				ASHE_DEFER(-1);
			/* FALLTHRU */
		case ARDOP_CLOSE:
			if (fd_assert_bounds(rdp->rd_lhsfd) < 0)
				ASHE_DEFER(-1);
			if (!exec)
				break;
			switch (rdp->rd_op) {
			case ARDOP_DUP_OUT:
				perms = O_WRONLY | O_RDWR;
				goto assertperms;
			case ARDOP_DUP_IN:
				perms = O_RDONLY | O_RDWR;
assertperms:
				if (fd_assert_perms(rdp->rd_rhsfd, perms) < 0)
					ASHE_DEFER(-1);
				if (ASHE_UNLIKELY(ashe_dup2(rdp->rd_rhsfd,
							    rdp->rd_lhsfd) < 0))
					ASHE_DEFER(-1);
				set_if_dirty(rdp->rd_lhsfd);
				break;
			default:
				ashe_assert(rdp->rd_op == ARDOP_CLOSE);
				ashe_assert(rdp->rd_lhsfd != -1);
				ashe_assert(rdp->rd_rhsfd == -1);
				ashe_assert(rdp->rd_fname == NULL);

				if (ASHE_UNLIKELY(ashe_close(rdp->rd_lhsfd) < 0))
					ASHE_DEFER(-1);
				set_if_dirty(rdp->rd_lhsfd);
				break;
			}
			break;
		default:
			/* UNREACHED */
			ashe_assert(0);
			break;
		}
	}
defer:
	return status;
}

ASHE_PRIVATE inline void stdfds_backup(a_int32 in, a_int32 out, a_int32 err)
{
	if (ASHE_UNLIKELY(ashe_dup2(STDIN_FILENO, in) < 0 ||
			  ashe_dup2(STDOUT_FILENO, out) < 0 ||
			  ashe_dup2(STDERR_FILENO, err) < 0))
		ashe_panic("can't backup file standard descriptors");
}

ASHE_PRIVATE inline void stdfds_restore(a_int32 in, a_int32 out, a_int32 err)
{
	const char *errfd;

	if (ASHE_UNLIKELY(!ashe.sh_dirtyfd[STDIN_FILENO] &&
			  ashe_dup2(in, STDIN_FILENO) < 0)) {
		errfd = "can't restore stdin";
		goto panic;
	}
	if (ASHE_UNLIKELY(!ashe.sh_dirtyfd[STDOUT_FILENO] &&
			  ashe_dup2(out, STDOUT_FILENO) < 0)) {
		errfd = "can't restore stdout";
		goto panic;
	}
	if (ASHE_UNLIKELY(!ashe.sh_dirtyfd[STDERR_FILENO] &&
			  ashe_dup2(err, STDERR_FILENO) < 0)) {
		errfd = "can't restore stderr";
		goto panic;
	}
	return;
panic:
	ashe_panic(errfd);
}

// clang-format off
ASHE_PRIVATE inline void extrafds_close(a_int32 fd1, a_int32 fd2, a_int32 fd3)
{
	if (ASHE_UNLIKELY(
		(!ashe.sh_dirtyfd[STDIN_FILENO] && ashe_close(fd1) < 0) ||
		(!ashe.sh_dirtyfd[STDOUT_FILENO] && ashe_close(fd2) < 0) ||
	    	(!ashe.sh_dirtyfd[STDERR_FILENO] && ashe_close(fd3) < 0)))
		goto panic;
	reset_dirtyfd();
	return;
panic:
	ashe_panic("can't close extra file descriptors");
}
// clang-format on

/* This runs a built-in command or puts
 * environment variables into 'environ' or both. */
ASHE_PRIVATE a_int32 run_scmd_nofork(struct a_simple_cmd *scmd,
				     enum a_builtin_type type)
{
	a_int32 status;
	a_int32 in, out, err;

	status = 0;
	in = ASHE_FD_0;
	out = ASHE_FD_1;
	err = ASHE_FD_2;

	if (ASHE_UNLIKELY(add_envs(&scmd->sc_env) == -1))
		ASHE_DEFER(-1);

	stdfds_backup(in, out, err);

	if (resolve_redirections(&scmd->sc_rds, type == TBI_EXEC) < 0) {
		reset_dirtyfd();
		status = -1;
	} else if (ARGC(scmd) > 0) {
		status = ashe_runbin(scmd, type);
	}

	stdfds_restore(in, out, err);
	extrafds_close(in, out, err);

	if (ASHE_UNLIKELY(rm_envs(&scmd->sc_env) < 0))
		status = -1;
defer:
	return status;
}

ASHE_PRIVATE a_int32 reset_signal_handling(void)
{
	struct sigaction sigdfl_ac;

	sigemptyset(&sigdfl_ac.sa_mask);
	sigdfl_ac.sa_flags = 0;
	sigdfl_ac.sa_handler = SIG_DFL;
	if (ASHE_UNLIKELY(sigaction(SIGINT, &sigdfl_ac, NULL) < 0 ||
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

ASHE_PRIVATE inline a_int32 connect_pipe(struct a_pipectx *ctx)
{
	if (ASHE_UNLIKELY(ashe_dup2(ctx->pipefd[PIPE_R], STDIN_FILENO) < 0 ||
			  ashe_dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO) < 0 ||
			  (ctx->closefd != -1 && ashe_close(ctx->closefd) < 0)))
		return -1;
	return 0;
}

ASHE_PRIVATE inline a_int32 scmd_exec(struct a_simple_cmd *scmd)
{
	char **argv;

	argv = acalloc(ARGC(scmd) + 1, sizeof(char *));
	memcpy(argv, scmd->sc_argv.data, sizeof(char *) * ARGC(scmd));
	argv[a_arr_len(scmd->sc_argv)] = NULL;

	if (execvp(argv[0], argv) < 0) {
		if (errno == ENOENT)
			ashe_eprintf("unknown command '%s'", argv[0]);
		else
			ashe_perrno("execvp");
		afree(argv);
		return -1;
	}

	return 0;
}

/*
 * 'pipes' are passed just to cleanup them up in case
 * of errors inside the forked process.
 */
// clang-format off
ASHE_PRIVATE a_int32 run_scmd_fork(struct a_simple_cmd *scmd, struct a_pipectx *ctx,
				   struct a_job *job, a_int32 *pipes)
{
	a_arr_ccharp *aargv = &scmd->sc_argv;
	a_arr_ccharp *aenv = &scmd->sc_env;
	a_uint32 argc;
	a_int32 type;
	a_pid PID;

	PID = fork();

	if (ASHE_UNLIKELY(PID < 0)) {
		ashe_perrno("fork failed");
		return -1;
	} else if (PID != 0) {
		if (job->pgid == 0)
			job->pgid = PID;
		/* This is also done in the fork to prevent race. */
		if (ASHE_UNLIKELY(setpgid(PID, job->pgid) < 0)) {
			ashe_perrno("failed setting process group ID (%d)", job->pgid);
			return -1;
		}
		return PID;
	}
	/* fork */
	ashe.sh_flags.isfork = 1;
	argc = a_arrp_len(aargv);

	if (ASHE_UNLIKELY(argc == 0 || add_envs(aenv) < 0))
		ASHE_DEFER_NO_STATUS();

	PID = getpid();

	if (job->pgid == 0) {
		job->pgid = PID;
		if (ASHE_UNLIKELY(job->foreground && tcsetpgrp(STDIN_FILENO, job->pgid) < 0)) {
			ashe_perrno("tcsetpgrp(%d, %d) failed", STDIN_FILENO, job->pgid);
			ASHE_DEFER_NO_STATUS();
		}
	}

	if (ASHE_UNLIKELY(setpgid(PID, job->pgid) < 0)) {
		ashe_perrno("failed setting process pgid (%d)", job->pgid);
		ASHE_DEFER_NO_STATUS();
	}
	if (ASHE_UNLIKELY(reset_signal_handling() < 0))
		ASHE_DEFER_NO_STATUS();

	type = ashe_isbin(ARGV(scmd, 0));

	if (connect_pipe(ctx) < 0 || resolve_redirections(&scmd->sc_rds, type == TBI_EXEC) < 0)
		ASHE_DEFER_NO_STATUS();

	if (type != -1) {
		if (pipes)
			afree(pipes);
		a_job_free(job);
		_exit(ashe_runbin(scmd, type));
	}

	if (scmd_exec(scmd) < 0) {
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
// clang-format on

ASHE_PRIVATE inline a_int32 close_pipe(a_int32 *pipe)
{
	if (ASHE_UNLIKELY(ashe_close(pipe[PIPE_R]) < 0 ||
			  ashe_close(pipe[PIPE_W]) < 0))
		return -1;
	return 0;
}

ASHE_PRIVATE a_int32 a_run_simple_cmd(struct a_simple_cmd *scmd, struct a_job *job,
				      a_uint32 i, a_int32 *pipes, a_uint32 cmdcnt)
{
	struct a_process proc;
	struct a_pipectx ctx;
	a_int32 type, status;
	a_pid PID;

	status = 1;
	a_pipectx_init(&ctx);

	if (cmdcnt > 1) {
		ashe_assert(pipes != NULL);
		if (ASHE_UNLIKELY(conf_pipe(pipes, cmdcnt, i, &ctx) < 0))
			ASHE_DEFER(-1);
	} else if (job->foreground &&
		   (ARGC(scmd) == 0 || (type = ashe_isbin(ARGV(scmd, 0))) >= 0)) {
		a_job_free(job);
		return run_scmd_nofork(scmd, type);
	}

	if (ASHE_UNLIKELY((PID = run_scmd_fork(scmd, &ctx, job, pipes)) < 0))
		ASHE_DEFER(-1);

	a_process_init(&proc, PID);
	a_job_add_process(job, proc);

	if (ASHE_UNLIKELY(i != 0 && close_pipe(&pipes[(i - 1) * 2]) < 0))
		ASHE_DEFER(-1);
defer:
	if (ASHE_UNLIKELY(status < 0)) {
		a_job_free(job);
		if (pipes)
			afree(pipes);
		ashe_panic(NULL);
	}
	return status;
}

ASHE_PRIVATE a_int32 a_run_cmd(struct a_cmd *cmd, struct a_job *job, a_uint32 i,
			       a_int32 *pipes, a_uint32 cmdcnt)
{
	ashe_assert(cmd != NULL);
	ashe_assert(job != NULL);

	switch (cmd->c_type) {
	case ACMD_SIMPLE:
		return a_run_simple_cmd(&cmd->c_u.scmd, job, i, pipes, cmdcnt);
	default:
		/* UNREACHED */
		ashe_assert(0);
	}
}

ASHE_PRIVATE a_int32 a_run_pipeline(struct a_pipeline *restrict pipeline)
{
	a_arr_cmd *cmds;
	struct a_cmd *cmd;
	struct a_job job;
	a_int32 *pipes;
	a_int32 status;
	a_uint32 cmdcnt, pn, i;
	a_ubyte stopped;

	cmds = &pipeline->pl_cmds;
	a_job_init(&job, pipeline->pl_input, pipeline->pl_bg);

	ashe_assert(job.foreground == !pipeline->pl_bg);
	ashe_assert(job.input != NULL);

	if ((cmdcnt = a_arrp_len(cmds)) > 1) {
		pn = ((cmdcnt - 1) * 2);
		pipes = amalloc(sizeof(a_int32) * pn);
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

ASHE_PRIVATE a_int32 a_run_list(struct a_list *restrict list)
{
	a_arr_pipeline *pipes;
	struct a_pipeline *pipeline;
	a_int32 status;
	a_uint32 i;

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

ASHE_PUBLIC a_int32 ashe_interpret(struct a_block *restrict block)
{
	a_memmax i;
	a_uint32 listcnt;
	a_int32 status;
	struct a_list *list;

	status = 0;
	listcnt = block->bl_lists.len;
	for (i = 0; i < listcnt; i++) {
		list = a_arr_list_index(&block->bl_lists, i);
		status = a_run_list(list);
	}
	return status;
}
