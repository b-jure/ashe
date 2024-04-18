#include "ajobcntl.h"
#include "acommon.h"
#include "aconf.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "ashell.h"
#include "autils.h"

#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(ASHE_WAIT_BEFORE_HARVEST_MS) || ASHE_WAIT_BEFORE_HARVEST_MS < 0
#undef ASHE_WAIT_BEFORE_HARVEST_MS
#define ASHE_WAIT_BEFORE_HARVEST_MS 100
#endif

/* ==== PROCESS ==== */

/* Initialize the 'proc' with 'pid' */
ASHE_PUBLIC void a_process_init(struct a_process *proc, a_pid PID)
{
	proc->pid = PID;
	proc->status = 0;
	proc->stopped = 0;
	proc->completed = 0;
}

/* ==== JOB ==== */

/*
 * Initialize the 'job', store 'input' for debug and
 * 'bg' is a flag indicating if this job is running in background.
 */
ASHE_PUBLIC void a_job_init(struct a_job *job, const char *input, a_ubyte bg)
{
	job->tmodes = ashe.sh_term.tm_dfltermios;
	a_arr_process_init(&job->processes);
	job->id = 0;
	job->pgid = 0;
	job->notified = 0;
	job->foreground = !bg;
	job->input = input;
}

/* Gets process at 'i'. */
ASHE_PRIVATE inline struct a_process *a_job_get_process(struct a_job *job, a_memmax i)
{
	return a_arr_process_index(&job->processes, i);
}

/* Returns 1 if 'job' contains process with 'pid', 0 otherwise. */
ASHE_PRIVATE inline a_ubyte a_job_contains_pid(struct a_job *job, a_pid pid)
{
	a_memmax jobcnt;

	jobcnt = a_job_processes(job);
	while (jobcnt--)
		if (a_job_get_process(job, jobcnt)->pid == pid)
			return 1;
	return 0;
}

/* Returns the number of processes currently in the 'job'. */
ASHE_PUBLIC a_memmax a_job_processes(struct a_job *job)
{
	return a_arr_len(job->processes);
}

/* Add 'process' into 'job'. */
ASHE_PUBLIC void a_job_add_process(struct a_job *job, struct a_process process)
{
	a_arr_process_push(&job->processes, process);
}

/* Return 1 if 'job' is stopped, otherwise 0. */
ASHE_PUBLIC a_ubyte a_job_is_stopped(struct a_job *job)
{
	a_memmax proc_cnt, i;

	proc_cnt = a_job_processes(job);
	for (i = 0; i < proc_cnt; i++)
		if (a_job_get_process(job, i)->stopped)
			return 1;
	return 0;
}

/* Return 1 if 'job' is completed, otherwise 0. */
ASHE_PUBLIC a_ubyte a_job_is_completed(struct a_job *job)
{
	a_memmax proc_cnt, i;

	proc_cnt = a_job_processes(job);
	for (i = 0; i < proc_cnt; i++)
		if (!a_job_get_process(job, i)->completed)
			return 0;
	return 1;
}

/*
 * Marks the 'job' as running in the background.
 * If 'cont' is non zero then SIGCONT signal is
 * sent to the job's process group ID.
 */
ASHE_PUBLIC void a_job_mark_as_background(struct a_job *job, a_ubyte cont)
{
	job->foreground = 0;
	if (cont)
		ashe_kill(-job->pgid, SIGCONT);
}

/*
 * Return string representation of 'sig'.
 * This function only handles signals specified by ISO C.
 * Auxiliary to 'a_jobcntl_update_process()'.
 */
ASHE_PRIVATE const char *sigstr(a_int32 sig)
{
	switch (sig) {
	case SIGABRT:
		return "ABRT";
	case SIGFPE:
		return "FPE";
	case SIGILL:
		return "ILL";
	case SIGINT:
		return "INT";
	case SIGSEGV:
		return "SEGV";
	case SIGTERM:
		return "TERM";
	default:
		return "?";
	}
}

/* Auxiliary to 'find_process_and_update_status()' */
ASHE_PRIVATE void proc_term_notify(struct a_process *proc, a_int32 status, a_ubyte notify)
{
	const char *signame;
	a_ubyte polite = 0;

	switch (WTERMSIG(status)) {
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGKILL:
	case SIGTERM:
		polite = 1;
		break;
	default:
		break;
	}

	proc->status = WTERMSIG(status);
	if (notify) {
		signame = sigstr(status);
		ashe_pinfo("PID %n was terminated by SIG%s%s", proc->pid, signame ? signame : "?",
			   (polite ? " (polite)" : ""));
	} else { /* print new line */
		ashe_print("\r\n", stderr);
	}
}

/* Update process status and notify if 'notify' is set */
ASHE_PRIVATE void Process_update_status(struct a_process *proc, a_int32 status, a_ubyte notify)
{
	proc->status = status;
	if (WIFSTOPPED(status)) {
		ashe_assert(proc->completed == 0);
		proc->stopped = 1;
	} else {
		ashe_assert(proc->stopped == 0);
		proc->completed = 1;
		if (WIFSIGNALED(status))
			proc_term_notify(proc, status, notify);
		else if (WIFEXITED(status))
			proc->status = WEXITSTATUS(status);
	}
}

/*
 * Updates the process 'pid' that is part of the 'job'.
 * Returns the updated process (with matchind 'pid') or
 * NULL if no process was found with the given 'pid'.
 */
ASHE_PRIVATE struct a_process *a_job_update_process_status(struct a_job *job, a_pid pid,
							   a_int32 status)
{
	struct a_process *proc;
	a_memmax i, proc_cnt;

	ashe_assert(pid > 0);
	proc_cnt = a_job_processes(job);

	for (i = 0; i < proc_cnt; i++) {
		proc = a_job_get_process(job, i);
		if (proc->pid == pid) {
			Process_update_status(proc, status, !job->foreground);
			return proc;
		}
	}
	return NULL;
}

/*
 * Waits for the 'job' to finish or until it gets paused.
 * Auxiliary to 'a_job_move_to_foreground()'.
 */
ASHE_PRIVATE a_int32 a_job_wait(struct a_job *job, a_ubyte *stop)
{
	a_pid pid;
	a_int32 status;
	struct a_process *proc;

	do {
		pid = ashe_waitpid(-job->pgid, &status, WUNTRACED);
		ashe_assert(pid >= 0);
		proc = a_job_update_process_status(job, pid, status);
		ashe_assert(proc != NULL);
	} while (!(*stop = proc->stopped) && !(*stop = a_job_is_stopped(job)) &&
		 !a_job_is_completed(job));

	if (!*stop)
		return a_arr_process_last(&job->processes)->status;
	else
		return 0;
}

/*
 * Moves the 'job' into foreground.
 * If 'cont' is non zero then SIGCONT signal
 * is sent to the 'job's process group ID.
 */
ASHE_PUBLIC a_int32 a_job_move_to_foreground(struct a_job *job, const a_ubyte cont, a_ubyte *stop)
{
	a_int32 status;

	status = 0;
	*stop = 0;
	job->foreground = 1;

	ashe_tcsetpgrp(job->pgid);
	if (cont)
		ashe_tcsetattr(TCSADRAIN, &job->tmodes);
	ashe_kill(-job->pgid, SIGCONT);

	status = a_job_wait(job, stop);
	job->foreground = 0; /* either stopped or completed */
	job->notified = !*stop; /* if completed, don't notify */

	if (!cont && *stop)
		a_jobcntl_add_job(&ashe.sh_jobcntl, job);

	ashe_tcsetpgrp(getpgrp());
	ashe_tcgetattr(&job->tmodes);
	ashe_tcsetattr(TCSADRAIN, &A_TM.tm_dfltermios);

	return status;
}

/*
 * Set 'job' as running by setting all processes that
 * belong to the 'job' as not stopped.
 * Auxiliary to 'a_job_continue()'.
 */
ASHE_PRIVATE inline void a_job_set_as_running(struct a_job *job)
{
	a_memmax i, len;

	len = a_job_processes(job);

	for (i = 0; i < len; i++)
		a_job_get_process(job, i)->stopped = 0;
	job->notified = 0;
}

/*
 * Sets the 'job' as running and if 'isfg' is a non zero
 * value then the job is moved into foreground.
 * If 'isfg' is 0, then the job is not moved into foreground
 * and is marked as background.
 * Additionally SIGCONT signal is sent to the job's process
 * group ID in case the 'job' was stopped.
 */
ASHE_PUBLIC void a_job_continue(struct a_job *job, a_ubyte isfg)
{
	a_ubyte stopped;

	a_job_set_as_running(job);
	if (isfg) {
		stopped = 0;
		a_job_move_to_foreground(job, 1, &stopped);
		if (ASHE_UNLIKELY(!stopped && !a_jobcntl_remove_job(&ashe.sh_jobcntl, job, NULL)))
			ashe_panic("job not found");
	} else {
		a_job_mark_as_background(job, 1);
	}
}

/* Frees the 'job' resources. */
ASHE_PUBLIC void a_job_free(struct a_job *job)
{
	a_arr_process_free(&job->processes, NULL);
	ashe_free((void *)job->input);
}

/* ==== JOB-CONTROL ==== */

/* Initialize the 'jobcntl'. */
ASHE_PUBLIC void a_jobcntl_init(struct a_jobcntl *jobcntl)
{
	a_arr_job_init(&jobcntl->jobs);
}

/* Return the number of jobs in 'jobcntl'. */
ASHE_PUBLIC a_memmax a_jobcntl_jobs(struct a_jobcntl *jobcntl)
{
	return a_arr_len(jobcntl->jobs);
}

/*
 * Generate new id.
 * Auxiliary to 'a_jobcntl_add_job()'.
 */
ASHE_PRIVATE inline a_memmax Joblist_id(struct a_jobcntl *jobcntl)
{
	static a_int32 id = 1;

	if (a_arr_len(jobcntl->jobs) == 0)
		id = 1;
	return id++;
}

/* Add 'job' to 'jobcntl' and assign it new id. */
ASHE_PUBLIC void a_jobcntl_add_job(struct a_jobcntl *jobcntl, struct a_job *job)
{
	job->id = Joblist_id(jobcntl);
	a_arr_job_push(&jobcntl->jobs, *job);
}

/*
 * Remove 'Job' from 'jobcntl' located at index 'i'.
 * Auxiliary to 'a_jobcntl_remove_job()'.
 */
ASHE_PRIVATE inline struct a_job a_jobcntl_remove(struct a_jobcntl *jobcntl, a_uint32 i)
{
	return a_arr_job_remove(&jobcntl->jobs, i);
}

/* Getter, returns Job at 'i' located in 'jobcntl'. */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_at(struct a_jobcntl *jobcntl, a_uint32 i)
{
	return a_arr_job_index(&jobcntl->jobs, i);
}

/*
 * Remove 'job' from 'jobcntl'.
 * If 'job' was found, remove it and free its resources, this returns 1.
 * Return 0 if the job was not found.
 */
ASHE_PUBLIC a_ubyte a_jobcntl_remove_job(struct a_jobcntl *jobcntl, struct a_job *job,
					 struct a_job *out)
{
	a_memmax jobcnt, i;

	jobcnt = a_jobcntl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		if (a_jobcntl_get_job_at(jobcntl, i)->pgid == job->pgid) {
			if (out != NULL)
				*out = a_jobcntl_remove(jobcntl, i);
			return 1;
		}
	}
	return 0;
}

/*
 * Update process corresponding to 'pid' with 'status'.
 * Additionally report if the process was terminated by a signal.
 * Auxiliary to 'a_jobcntl_update()'.
 */
ASHE_PRIVATE a_int32 a_jobcntl_update_process(struct a_jobcntl *jobcntl, a_pid pid, a_int32 status)
{
	a_memmax jobcnt, i;
	struct a_job *job;

	if (pid == 0) /* WNOHANG specified (process stopped) */
		return 0;

	jobcnt = a_jobcntl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		if (a_job_update_process_status(job, pid, status) != NULL)
			return 1;
	}
	return -1;
}

/*
 * Wait for any child process to stop or terminate and update it.
 * This runs until there are no any process that stopped or were terminated.
 * Auxiliary to 'a_jobcntl_update_and_notify'.
 */
ASHE_PRIVATE void a_jobcntl_update(struct a_jobcntl *jobcntl)
{
	a_int32 status, ret;
	a_pid pid;

	do {
		pid = ashe_waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
		if (pid < 0) { /* ECHILD */
			errno = 0;
			break;
		}
		ret = a_jobcntl_update_process(jobcntl, pid, status);
		ashe_assert(ret >= 0);
	} while (pid > 0);
}

/*
 * Updates the 'JobControl' by removing all of the finished jobs.
 * Additionally reports any of the jobs that were completed and/or stopped.
 * In most cases gets called inside the signal handler, so we have to take
 * care of the terminal input and about redrawing the screen properly.
 * Calling this directly without masking off the other signals with
 * SIG_BLOCK is highly unsafe and will probably break something.
 */
ASHE_PUBLIC void a_jobcntl_update_and_notify(struct a_jobcntl *jobcntl)
{
	struct a_term *term = &ashe.sh_term;
	a_uint32 col, row, idx;
	a_memmax jobcnt, i;
	struct a_job *job, out;
	a_ubyte completed;

	if ((jobcnt = a_jobcntl_jobs(jobcntl)) == 0)
		return;

	a_jobcntl_update(jobcntl);

	for (i = 0; i < jobcnt;) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		completed = 0;

		if (a_job_is_completed(job)) {
			completed = 1;
			out = a_jobcntl_remove(jobcntl, i);
			job = &out;
			jobcnt--;
			if (!job->foreground && !job->notified)
				goto notify;
			a_job_free(job);
			continue;
		} else if (a_job_is_stopped(job) && !job->notified) {
notify:
			if (ashe.sh_term.tm_reading) { /* in signal handler ? */
				col = A_ICOL;
				row = A_IROW;
				idx = A_IBFIDX;
				ashe_c_end();
				ashe_print("\r\n", stderr);
			}

			ashe_pinfo("[%n] '%s' %s", job->pgid, job->input,
				   (completed ? "<completed>" : "<stopped>"));
			ashe_p_draw_unsafe();

			if (term->tm_reading) {
				A_ICOL = col;
				A_IROW = row;
				A_IBFIDX = idx;
				ashe_i_redraw_unsafe();
				a_term_sync_cursor();
			}

			if (completed) {
				a_job_free(job);
				continue;
			}
			job->notified = 1;
		}
		i++;
	}
}

/*
 * Returns address of a Job that has a matching 'id' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL.
 */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_id(struct a_jobcntl *jobcntl, a_memmax id)
{
	a_memmax jobcnt, i;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		if (job->id == id)
			return job;
	}
	return NULL;
}

/*
 * Returns address of a Job that contains a process with matching
 * 'pid' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL.
 */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_pid(struct a_jobcntl *jobcntl, a_pid pid)
{
	a_memmax jobcnt, proc_cnt, i, j;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		proc_cnt = a_job_processes(job);
		for (j = 0; j < proc_cnt; j++)
			if (a_job_get_process(job, j)->pid == pid)
				return job;
	}
	return NULL;
}

/*
 * Returns address of a Job that has a matching 'pgid' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL.
 */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_pgid(struct a_jobcntl *jobcntl, a_pid pgid)
{
	a_memmax jobcnt, i;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);

	for (i = 0; i < jobcnt; i++) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		if (job->pgid == pgid)
			return job;
	}

	return NULL;
}

/*
 * Gets the Job from 'jobcntl' that matches the 'where' flag.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where' is zero then only a background job can be returned.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out).
 */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_from(struct a_jobcntl *jobcntl, a_ubyte where)
{
	a_memmax jobcnt;
	struct a_job *job;

	where = (where != 0);
	jobcnt = a_jobcntl_jobs(jobcntl);

	while (jobcnt--) {
		job = a_jobcntl_get_job_at(jobcntl, jobcnt);
		if (job->foreground == where)
			return job;
	}

	return NULL;
}

/*
 * Gets the Job from 'jobcntl' that matches 'where' and has matching 'id'.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where is zero then only a background job can be returned.
 * Job additionally must have matching 'id'.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out).
 */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_id_from(struct a_jobcntl *jobcntl, a_memmax id,
							 a_ubyte where)
{
	a_memmax jobcnt;
	struct a_job *job;

	where = (where != 0);
	jobcnt = a_jobcntl_jobs(jobcntl);

	while (jobcnt--) {
		job = a_jobcntl_get_job_at(jobcntl, jobcnt);
		if (job->id == id && job->foreground == where)
			return job;
	}

	return NULL;
}

/*
 * Gets the Job from 'jobcntl' that matches 'where' and 'pid'.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where is zero then only a background job can be returned.
 * Job additionally must have matching 'pid'.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out).
 */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_pid_from(struct a_jobcntl *jobcntl, a_pid pid,
							  a_ubyte where)
{
	a_memmax jobcnt;
	struct a_job *job;

	where = (where != 0);
	jobcnt = a_jobcntl_jobs(jobcntl);

	while (jobcnt--) {
		job = a_jobcntl_get_job_at(jobcntl, jobcnt);
		if (job->foreground == where && a_job_contains_pid(job, pid) != 0)
			return job;
	}

	return NULL;
}

/*
 * Gets the Job from 'jobcntl' that matches 'where' and has matching 'pgid'.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where is zero then only a background job can be returned.
 * Job additionally must have matching 'pgid'.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out).
 */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_pgid_from(struct a_jobcntl *jobcntl, a_pid pgid,
							   a_ubyte where)
{
	struct a_job *job;
	a_memmax jobcnt;

	where = (where != 0);
	jobcnt = a_jobcntl_jobs(jobcntl);

	while (jobcnt--) {
		job = a_jobcntl_get_job_at(jobcntl, jobcnt);
		if (job->pgid == pgid && job->foreground == where)
			return job;
	}
	return NULL;
}

/* Frees the memory occupied by 'jobcntl'. */
ASHE_PUBLIC void a_jobcntl_free(struct a_jobcntl *jobcntl)
{
	a_arr_job_free(&jobcntl->jobs, (FreeFn)a_job_free);
}

/* Sends SIGKILL signal to all the processes that belong to 'job'. */
ASHE_PRIVATE void a_job_kill_and_harvest(struct a_job *job)
{
	a_pid pid;
	struct timespec ts;
	a_memmax zombies, processes;

	/* do not call wrapper, it's okay if this fails*/
	if (ASHE_UNLIKELY(kill(-job->pgid, SIGKILL) < 0))
		ashe_perrno("kill");

	ts.tv_sec = 0;
	ts.tv_nsec = (ASHE_WAIT_BEFORE_HARVEST_MS % 1000) * 1000000;

	if (ASHE_UNLIKELY(nanosleep(&ts, NULL) < 0))
		ashe_perrno("nanosleep");

	zombies = 0;
	processes = a_job_processes(job);
	ashe_assert(processes > 0);

	do {
		pid = ashe_waitpid(-job->pgid, NULL, WUNTRACED);
		ashe_assert(pid >= 0);
		if (pid == 0)
			zombies++;
	} while (--processes > 0);

	if (ASHE_UNLIKELY(zombies > 0))
		ashe_eprintf("created %d zombie processes in PGID %d", zombies, job->pgid);
}

/*
 * Kills and harvests all processes belonging
 * to any job inside of 'jobcntl'.
 */
ASHE_PUBLIC void a_jobcntl_harvest(struct a_jobcntl *jobcntl)
{
	a_memmax len;
	struct a_job *job;

	len = a_jobcntl_jobs(jobcntl);

	while (len--) {
		job = a_jobcntl_get_job_at(jobcntl, len);
		a_job_kill_and_harvest(job);
	}
}
