/* ----------------------------------------------------------------------------------------------
 * Copyright (C) 2023-2024 Jure Bagić
 *
 * This file is part of ashe.
 * ashe is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ashe is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ashe.
 * If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------------------------*/

#include "aalloc.h"
#include "abuiltin.h"
#include "acommon.h"
#include "ajobcntl.h"
#include "autils.h"
#include "aparser.h"
#include "arun.h"
#include "ashell.h"
#include "aasync.h"
#include "alibc.h"

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

ASHE_PRIVATE inline void a_pipectx_init(struct a_pipectx *restrict ctx)
{
	ctx->pipefd[0] = STDIN_FILENO;
	ctx->pipefd[1] = STDOUT_FILENO;
	ctx->closefd = -1;
}

ASHE_PRIVATE void conf_pipe(a_int32 *restrict pipes, a_memmax len, a_memmax i,
			    struct a_pipectx *restrict ctx)
{
	a_int32 *poffset;

	if (i == 0) {
		poffset = pipes;
		ashe_pipe(poffset);
		ctx->pipefd[PIPE_W] = poffset[PIPE_W];
		ctx->closefd = poffset[PIPE_R];
	} else if (i != len - 1) {
		poffset = &pipes[i * 2];
		ashe_pipe(poffset);
		ctx->pipefd[PIPE_R] = poffset[-2];
		ctx->pipefd[PIPE_W] = poffset[PIPE_W];
		ctx->closefd = poffset[PIPE_R];
	} else {
		poffset = &pipes[--i * 2];
		ctx->pipefd[PIPE_R] = poffset[PIPE_R];
		ctx->closefd = poffset[PIPE_W];
	}
}

ASHE_PRIVATE void add_envs(const a_arr_ccharp *restrict env)
{
	char *name, *sep, *value;
	a_memmax len, i;

	len = env->len;
	for (i = 0; i < len; i++) {
		name = ashe_dupstr(*a_arr_ccharp_index(env, i));
		sep = strchr(name, '=');
		value = sep + 1;
		*sep = '\0';
		ashe_setenv(name, value, 1);
		*a_arr_ccharp_index(env, i) = name;
		a_arr_ccharp_push(&ashe.sh_strings, name);
	}
}

ASHE_PRIVATE void rm_envs(const a_arr_ccharp *restrict env)
{
	const char *key;
	a_memmax len, i;

	len = a_arrp_len(env);

	for (i = 0; i < len; i++) {
		key = *a_arr_ccharp_index(env, i);
		ashe_assert(key != NULL);
		ashe_assert(strchr(key, '=') == NULL);
		/* env is already in format 'key\0value', check add_envs()*/
		unsetenv(key); /* can't fail */
	}
}

ASHE_PRIVATE inline void redirect(a_int32 oldfd, a_int32 newfd)
{
	ashe_dup2(oldfd, newfd);
	ashe_close(oldfd);
}

ASHE_PRIVATE inline void redirect_errout(a_int32 fd)
{
	ashe_dup2(fd, STDERR_FILENO);
	ashe_dup2(fd, STDOUT_FILENO);
	ashe_close(fd);
}

ASHE_PRIVATE inline void setdirty(a_int32 fd)
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
		ashe_eprintf("file descriptor %n out of bounds.", fd);
		return -1;
	}
	return 0;
}

ASHE_PRIVATE inline a_int32 fd_assert_perms(a_int32 fd, a_memmax perms)
{
	if (!a_fd_isopen(fd, perms)) {
		ashe_eprintf("file descriptor %n lacks required permissions.", fd);
		return -1;
	}
	return 0;
}

ASHE_PRIVATE inline a_int32 fd_assert_valid(a_int32 fd)
{
	errno = 0;
	if (fcntl(fd, F_GETFD) < 0 || errno == EBADFD) {
		ashe_perrno("bad file descriptor %n");
		return -1;
	}
	return 0;
}

ASHE_PRIVATE a_int32 resolve_redirections(a_arr_redirect *restrict rds, a_ubyte exec)
{
	a_ssize fd;
	struct a_redirect *rdp;
	a_memmax len, i, perms;
	a_int32 status;
	a_ubyte how;

	status = 0;
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
			if ((fd = ashe_open(rdp->rd_fname, AHOW_W, rdp->rd_append)) < 0)
				a_defer(-1);
			redirect_errout(fd);
			break;
		case ARDOP_REDIRECT_INOUT:
			ashe_assert(rdp->rd_append == 0);
			ashe_assert(rdp->rd_rhsfd == -1);
			ashe_assert(rdp->rd_lhsfd != -1);
			ashe_assert(rdp->rd_fname);
			fd = ashe_open(rdp->rd_fname, AHOW_RW, rdp->rd_append);
			if (fd < 0 || fd_assert_bounds(rdp->rd_lhsfd) < 0)
				a_defer(-1);
			if (!exec) {
				ashe_close(fd);
				break;
			}
			if (fd_assert_valid(rdp->rd_lhsfd) < 0)
				a_defer(-1);
			redirect(fd, rdp->rd_lhsfd);
			setdirty(rdp->rd_lhsfd);
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
				a_defer(-1);
			redirect(fd, rdp->rd_lhsfd);
			break;
		case ARDOP_DUP_IN:
		case ARDOP_DUP_OUT:
			ashe_assert(rdp->rd_lhsfd != -1);
			ashe_assert(rdp->rd_rhsfd != -1);
			ashe_assert(rdp->rd_fname == NULL);
			if (fd_assert_bounds(rdp->rd_rhsfd) < 0)
				a_defer(-1);
			/* FALLTHRU */
		case ARDOP_CLOSE:
			if (fd_assert_bounds(rdp->rd_lhsfd) < 0)
				a_defer(-1);
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
					a_defer(-1);
				ashe_dup2(rdp->rd_rhsfd, rdp->rd_lhsfd);
				setdirty(rdp->rd_lhsfd);
				break;
			default:
				ashe_assert(rdp->rd_op == ARDOP_CLOSE);
				ashe_assert(rdp->rd_lhsfd != -1);
				ashe_assert(rdp->rd_rhsfd == -1);
				ashe_assert(rdp->rd_fname == NULL);
				ashe_close(rdp->rd_lhsfd);
				setdirty(rdp->rd_lhsfd);
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

ASHE_PRIVATE inline void stdfd_backup(a_int32 in, a_int32 out, a_int32 err)
{
	ashe_dup2(STDIN_FILENO, in);
	ashe_dup2(STDOUT_FILENO, out);
	ashe_dup2(STDERR_FILENO, err);
}

ASHE_PRIVATE inline void stdfd_restore(a_int32 in, a_int32 out, a_int32 err)
{
	if (!ashe.sh_dirtyfd[STDIN_FILENO])
		ashe_dup2(in, STDIN_FILENO);
	if (!ashe.sh_dirtyfd[STDOUT_FILENO])
		ashe_dup2(out, STDOUT_FILENO);
	if (!ashe.sh_dirtyfd[STDERR_FILENO])
		ashe_dup2(err, STDERR_FILENO);
}

ASHE_PRIVATE inline void extrafds_close(a_int32 fd1, a_int32 fd2, a_int32 fd3)
{
	if (!ashe.sh_dirtyfd[STDIN_FILENO])
		ashe_close(fd1);
	if (!ashe.sh_dirtyfd[STDOUT_FILENO])
		ashe_close(fd2);
	if (!ashe.sh_dirtyfd[STDERR_FILENO])
		ashe_close(fd3);
	reset_dirtyfd();
}

/* This runs a built-in command or puts
 * environment variables into 'environ' or both. */
ASHE_PRIVATE a_int32 run_scmd_nofork(struct a_simple_cmd *restrict scmd, enum a_builtin_type type)
{
	a_int32 status;
	a_int32 in, out, err;

	status = 0;
	in = ASHE_FD_0;
	out = ASHE_FD_1;
	err = ASHE_FD_2;

	add_envs(&scmd->sc_env);
	stdfd_backup(in, out, err);

	if (resolve_redirections(&scmd->sc_rds, type == TBI_EXEC) < 0) {
		reset_dirtyfd();
		status = -1;
	} else if (ARGC(scmd) > 0) {
		status = ashe_runbin(scmd, type);
		rm_envs(&scmd->sc_env);
	}

	stdfd_restore(in, out, err);
	extrafds_close(in, out, err);
	return status;
}

ASHE_PRIVATE void reset_signal_handling(void)
{
	struct sigaction sigdfl_ac;

	sigemptyset(&sigdfl_ac.sa_mask);
	sigdfl_ac.sa_flags = 0;
	sigdfl_ac.sa_handler = SIG_DFL;
	ashe_sigaction(SIGINT, &sigdfl_ac, NULL);
	ashe_sigaction(SIGCHLD, &sigdfl_ac, NULL);
	ashe_sigaction(SIGWINCH, &sigdfl_ac, NULL);
	ashe_sigaction(SIGQUIT, &sigdfl_ac, NULL);
	ashe_sigaction(SIGTSTP, &sigdfl_ac, NULL);
	ashe_sigaction(SIGTTIN, &sigdfl_ac, NULL);
	ashe_sigaction(SIGTTOU, &sigdfl_ac, NULL);
	ashe_mask_signals(SIG_UNBLOCK);
}

ASHE_PRIVATE inline void connect_pipe(struct a_pipectx *restrict ctx)
{
	ashe_dup2(ctx->pipefd[PIPE_R], STDIN_FILENO);
	ashe_dup2(ctx->pipefd[PIPE_W], STDOUT_FILENO);
	if (ctx->closefd != -1)
		ashe_close(ctx->closefd);
}

ASHE_PRIVATE inline a_int32 scmd_exec(struct a_simple_cmd *restrict scmd)
{
	char **argv;

	argv = ashe_calloc(ARGC(scmd) + 1, sizeof(char *));
	memcpy(argv, a_arr_ptr(scmd->sc_argv), sizeof(char *) * ARGC(scmd));
	argv[ARGC(scmd)] = NULL;

	if (execvp(argv[0], argv) < 0) {
		if (errno == ENOENT)
			ashe_eprintf("unknown command '%s'", argv[0]);
		else
			ashe_perrno("execvp");
		ashe_free(argv);
		return -1;
	}

	return 0;
}

/* 'pipes' are passed just to cleanup them up in fork if possible */
ASHE_PRIVATE a_int32 run_scmd_fork(struct a_simple_cmd *restrict scmd,
				   struct a_pipectx *restrict ctx, struct a_job *restrict job,
				   a_int32 *restrict pipes)
{
	a_arr_ccharp *aargv = &scmd->sc_argv;
	a_arr_ccharp *aenv = &scmd->sc_env;
	a_uint32 argc;
	a_int32 type;
	a_int32 status;
	a_pid pid;

	pid = ashe_fork();

	if (pid > 0) {
		ashe_assert(pid > 0);
		if (job->pgid == 0)
			job->pgid = pid;
		/* This is also done in the fork to prevent race. */
		ashe_setpgid(pid, job->pgid);
		return pid;
	} /* else fork */

	ashe_assert(pid == 0);
	ashe.sh_flags.isfork = 1;
	argc = a_arrp_len(aargv);
	status = EXIT_FAILURE;

	pid = getpid();

	if (job->pgid == 0) {
		job->pgid = pid;
		if (job->foreground)
			ashe_tcsetpgrp(job->pgid);
	}

	ashe_setpgid(pid, job->pgid);
	reset_signal_handling();
	connect_pipe(ctx);
	add_envs(aenv);

	if (argc == 0) {
		status = EXIT_SUCCESS;
		goto cleanup;
	}

	type = ashe_isbin(ARGV(scmd, 0));

	if (resolve_redirections(&scmd->sc_rds, type == TBI_EXEC) < 0)
		goto cleanup;

	if (type != -1) {
		if (pipes)
			ashe_free(pipes);
		a_job_free(job);
		ashe_exit(ashe_runbin(scmd, type));
	}

	if (scmd_exec(scmd) < 0) {
cleanup:
		if (pipes)
			ashe_free(pipes);
		a_job_free(job);
		ashe_exit(status);
	}
	/* UNREACHED */
	ashe_assert(0);
	return 0;
}

ASHE_PRIVATE inline void close_pipe(a_int32 *restrict pipe)
{
	ashe_close(pipe[PIPE_R]);
	ashe_close(pipe[PIPE_W]);
}

ASHE_PRIVATE a_int32 a_run_simple_cmd(struct a_simple_cmd *restrict scmd,
				      struct a_job *restrict job, a_uint32 i, a_int32 *pipes,
				      a_uint32 cmdcnt)
{
	struct a_process proc;
	struct a_pipectx ctx;
	a_int32 type;
	a_pid pid;

	type = -1;
	a_pipectx_init(&ctx);

	if (cmdcnt > 1) {
		ashe_assert(pipes != NULL);
		conf_pipe(pipes, cmdcnt, i, &ctx);
	} else if (job->foreground &&
		   (ARGC(scmd) == 0 || (type = ashe_isbin(ARGV(scmd, 0))) >= 0)) {
		a_job_free(job);
		return run_scmd_nofork(scmd, type);
	}

	pid = run_scmd_fork(scmd, &ctx, job, pipes);
	a_process_init(&proc, pid);
	a_job_add_process(job, proc);

	if (i != 0)
		close_pipe(&pipes[(i - 1) * 2]);

	return 1; /* 1 if forked */
}

ASHE_PRIVATE a_int32 a_run_cmd(struct a_cmd *restrict cmd, struct a_job *restrict job, a_uint32 i,
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
		return 0;
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

	pipes = NULL;
	cmds = &pipeline->pl_cmds;
	a_job_init(&job, ashe_dupstr(pipeline->pl_input), pipeline->pl_bg);

	ashe_assert(job.foreground == !pipeline->pl_bg);
	ashe_assert(job.input != NULL);

	if ((cmdcnt = a_arrp_len(cmds)) > 1) {
		pn = ((cmdcnt - 1) * 2);
		pipes = ashe_malloc(sizeof(a_int32) * pn);
	}

	ashe_assert(cmdcnt >= 1);

	for (i = 0; i < cmdcnt; i++) {
		cmd = a_arr_cmd_index(cmds, i);
		status = a_run_cmd(cmd, &job, i, pipes, cmdcnt);

		if (a_likely(status == 1)) { /* forked ? */
			status = 0;
		} else { /* else single builtin foreground command */
			ashe_assert(status <= 0 && i == 0 && cmdcnt == 1);
			return status;
		}
	}

	if (job.foreground) {
		stopped = 0;
		status = a_job_move_to_foreground(&job, 0, &stopped);
		if (!stopped) /* job done ? */
			a_job_free(&job);
	} else {
		a_jobcntl_add_job(&ashe.sh_jobcntl, &job);
		a_job_mark_as_background(&job, 0);
	}

	if (cmdcnt > 1)
		ashe_free(pipes);

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

ASHE_PUBLIC a_int32 ashe_run(struct a_block *restrict block)
{
	a_memmax i;
	a_uint32 listcnt;
	a_int32 status;
	struct a_list *list;

	ashe_dprint("RE[P]L");
	status = 0;
	listcnt = block->bl_lists.len;
	for (i = 0; i < listcnt; i++) {
		list = a_arr_list_index(&block->bl_lists, i);
		status = a_run_list(list);
	}
	return status;
}
