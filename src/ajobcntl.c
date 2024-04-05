#include "ajobcntl.h"
#include "aconf.h"
#include "ainput.h"
#include "ajobcntl.h"
#include "ashell.h"
#include "aprompt.h"
#include "autils.h"

#include <errno.h>
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
ASHE_PUBLIC void a_process_init(struct a_process *proc, pid PID)
{
	proc->pid = PID;
	proc->status = 0;
	proc->stopped = 0;
	proc->completed = 0;
}

/* ==== JOB ==== */

/* Initialize the 'job', store 'input' for debug and
 * 'bg' is a flag indicating if this job is running in background. */
ASHE_PUBLIC void a_job_init(struct a_job *job, const char *input, ubyte bg)
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
ASHE_PRIVATE inline struct a_process *a_job_get_process(struct a_job *job,
							memmax i)
{
	return a_arr_process_index(&job->processes, i);
}

/* Returns 1 if 'job' contains process with 'pid', 0 otherwise. */
ASHE_PRIVATE inline ubyte a_job_contains_pid(struct a_job *job, pid pid)
{
	memmax jobcnt;

	jobcnt = a_job_processes(job);
	while (jobcnt--)
		if (a_job_get_process(job, jobcnt)->pid == pid)
			return 1;
	return 0;
}

/* Returns the number of processes currently in the 'job'. */
ASHE_PUBLIC memmax a_job_processes(struct a_job *job)
{
	return a_arr_process_len(&job->processes);
}

/* Add 'process' into 'job'. */
ASHE_PUBLIC void a_job_add_process(struct a_job *job, struct a_process process)
{
	a_arr_process_push(&job->processes, process);
}

/* Return 1 if 'job' is stopped, otherwise 0. */
ASHE_PUBLIC ubyte a_job_is_stopped(struct a_job *job)
{
	memmax proc_cnt, i;

	proc_cnt = a_job_processes(job);
	for (i = 0; i < proc_cnt; i++)
		if (a_job_get_process(job, i)->stopped)
			return 1;
	return 0;
}

/* Return 1 if 'job' is completed, otherwise 0. */
ASHE_PUBLIC ubyte a_job_is_completed(struct a_job *job)
{
	memmax proc_cnt, i;

	proc_cnt = a_job_processes(job);
	for (i = 0; i < proc_cnt; i++)
		if (!a_job_get_process(job, i)->completed)
			return 0;
	return 1;
}

/* Marks the 'job' as running in the background.
 * If 'cont' is non zero then SIGCONT signal is
 * sent to the job's process group ID. */
ASHE_PUBLIC void a_job_mark_as_background(struct a_job *job, ubyte cont)
{
	job->foreground = 0;
	if (unlikely(cont && kill(-job->pgid, SIGCONT) < 0))
		ashe_perrno();
}

/* Return string representation of 'sig'.
 * This function only handles signals specified by ISO C.
 * Auxiliary to 'a_jobcntl_update_process()'. */
ASHE_PRIVATE const char *sigstr(int32 sig)
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
		return NULL;
	}
}

/* Auxiliary to 'find_process_and_update_status()' */
ASHE_PRIVATE void proc_term_notify(struct a_process *proc, int32 status,
				   ubyte notify)
{
	const char *signame;
	ubyte polite = 0;

	switch (WTERMSIG(status)) {
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGKILL:
	case SIGTERM:
		polite = 1;
		/* FALLTHRU */
	default:
		break;
	}

	proc->status = WTERMSIG(status);
	if (notify) {
		signame = sigstr(status);
		printf_info("PID %d was terminated by SIG%s%s", proc->pid,
			    signame ? signame : "?",
			    (polite ? " (polite)" : ""));
	} else { /* print new line */
		fputs("\r\n", stderr);
		fflush(stderr);
	}
}

ASHE_PRIVATE void Process_update_status(struct a_process *proc, int32 status,
					ubyte notify)
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

/* Updates the process 'pid' that is part of the 'job'.
 * Returns the updated process (with matchind 'pid') or
 * NULL if no process was found with the given 'pid'. */
ASHE_PRIVATE struct a_process *
a_job_update_process_status(struct a_job *job, pid PID, int32 status)
{
	struct a_process *proc;
	memmax i, proc_cnt;

	ashe_assert(PID > 0);
	proc_cnt = a_job_processes(job);
	for (i = 0; i < proc_cnt; i++) {
		proc = a_job_get_process(job, i);
		if (proc->pid == PID) {
			Process_update_status(proc, status, !job->foreground);
			return proc;
		}
	}
	return NULL;
}

/* Waits for the 'job' to finish or until it gets paused.
 * Auxiliary to 'a_job_move_to_foreground()'. */
ASHE_PRIVATE int32 a_job_wait(struct a_job *job, ubyte *stop)
{
	pid PID;
	int32 status;
	struct a_process *proc;

	do {
		errno = 0;
		PID = waitpid(-job->pgid, &status, WUNTRACED);
		if (unlikely(PID < 0)) {
			ashe_perrno();
			panic(NULL);
		}
		proc = a_job_update_process_status(job, PID, status);
		ashe_assert(proc != NULL);
	} while (!proc->stopped && !(proc->stopped = a_job_is_stopped(job)) &&
		 !a_job_is_completed(job));

	if (!(*stop = proc->stopped))
		return a_arr_process_last(&job->processes)->status;
	else
		return 0;
}

/* Moves the 'job' into foreground.
 * If 'cont' is non zero then SIGCONT signal
 * is sent to the 'job's process group ID. */
ASHE_PUBLIC int32 a_job_move_to_foreground(struct a_job *job, ubyte cont,
					   ubyte *stop)
{
	*stop = 0;
	job->foreground = 1;
	if (unlikely(tcsetpgrp(STDIN_FILENO, job->pgid) < 0))
		goto l_panic;
	if (unlikely(cont &&
		     tcsetattr(STDIN_FILENO, TCSADRAIN, &job->tmodes) < 0))
		goto l_panic;
	if (unlikely(kill(-job->pgid, SIGCONT) < 0))
		goto l_panic;
	int32 status = a_job_wait(job, stop);
	/* 'cont' means the 'job' is already inside of the 'JobControl' */
	if (!cont && *stop)
		a_jobcntl_add_job(&ashe.sh_jobcntl, job);
	if (unlikely(tcsetpgrp(STDIN_FILENO, getpgrp()) < 0))
		goto l_panic;
	if (unlikely(tcgetattr(STDIN_FILENO, &job->tmodes) < 0))
		goto l_panic;
	if (unlikely(tcsetattr(STDIN_FILENO, TCSADRAIN,
			       &ashe.sh_term.tm_dfltermios) < 0))
		goto l_panic;
	return status;
l_panic:
	ashe_perrno();
	if (!cont)
		a_job_free(job);
	panic(NULL);
	return 0; /* UNREACHED */
}

/* Set 'job' as running by setting all processes that
 * belong to the 'job' as not stopped.
 * Auxiliary to 'a_job_continue()'. */
ASHE_PRIVATE inline void a_job_set_as_running(struct a_job *job)
{
	memmax i, len;

	len = a_job_processes(job);
	for (i = 0; i < len; i++)
		a_job_get_process(job, i)->stopped = 0;
	job->notified = 0;
}

/* Sets the 'job' as running and if 'isfg' is a non zero
 * value then the job is moved into foreground.
 * If 'isfg' is 0, then the job is not moved into foreground
 * and is marked as background.
 * Additionally SIGCONT signal is sent to the job's process
 * group ID in case the 'job' was stopped. */
ASHE_PUBLIC void a_job_continue(struct a_job *job, ubyte isfg)
{
	ubyte stopped = 0;

	a_job_set_as_running(job);
	if (isfg) {
		a_job_move_to_foreground(job, 1, &stopped);
		if (unlikely(!stopped && !a_jobcntl_remove_job(&ashe.sh_jobcntl,
							       job, NULL)))
			panic("Couldn't remove the job from 'JobControl'.");
	} else {
		a_job_mark_as_background(job, 1);
	}
}

/* Frees the 'job' resources. */
ASHE_PUBLIC void a_job_free(struct a_job *job)
{
	a_arr_process_free(&job->processes, NULL);
}

/* ==== JOB-CONTROL ==== */

/* Initialize the 'jobcntl'. */
ASHE_PUBLIC void a_jobcntl_init(struct a_jobcntl *jobcntl)
{
	a_arr_job_init(&jobcntl->jobs);
}

/* Return the number of jobs in 'jobcntl'. */
ASHE_PUBLIC memmax a_jobcntl_jobs(struct a_jobcntl *jobcntl)
{
	return a_arr_job_len(&jobcntl->jobs);
}

/* Generate new id.
 * Auxiliary to 'a_jobcntl_add_job()'. */
ASHE_PRIVATE inline memmax Joblist_id(struct a_jobcntl *jobcntl)
{
	static int32 id = 1;

	if (jobcntl->jobs.len == 0)
		id = 1;
	return id++;
}

/* Add 'job' to 'jobcntl' and assign it new id. */
ASHE_PUBLIC void a_jobcntl_add_job(struct a_jobcntl *jobcntl, struct a_job *job)
{
	job->id = Joblist_id(jobcntl);
	a_arr_job_push(&jobcntl->jobs, *job);
}

/* Remove 'Job' from 'jobcntl' located at index 'i'.
 * Auxiliary to 'a_jobcntl_remove_job()'. */
ASHE_PRIVATE inline struct a_job a_jobcntl_remove(struct a_jobcntl *jobcntl,
						  uint32 i)
{
	return a_arr_job_remove(&jobcntl->jobs, i);
}

/* Getter, returns Job at 'i' located in 'jobcntl'. */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_at(struct a_jobcntl *jobcntl,
					       uint32 i)
{
	return a_arr_job_index(&jobcntl->jobs, i);
}

/* Remove 'job' from 'jobcntl'.
 * If 'job' was found, remove it and free its resources, this returns 1.
 * Return 0 if the job was not found. */
ASHE_PUBLIC ubyte a_jobcntl_remove_job(struct a_jobcntl *jobcntl,
				       struct a_job *job, struct a_job *out)
{
	memmax jobcnt, i;

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
/* Update process corresponding to 'pid' with 'status'.
 * Additionally report if the process was terminated by a signal.
 * Auxiliary to 'a_jobcntl_update()'. */
ASHE_PRIVATE int32 a_jobcntl_update_process(struct a_jobcntl *jobcntl, pid pid,
					    int32 status)
{
	memmax jobcnt, i;
	struct a_job *job;

	if (pid <= 0) {
		if (pid != 0 && errno != ECHILD)
			ashe_perrno();
		return 0;
	}

	jobcnt = a_jobcntl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		if (a_job_update_process_status(job, pid, status) != NULL)
			return 1;
	}
	return -1;
}

/* Wait for any child process to stop or terminate and update it.
 * This runs until there are no any process that stopped or were terminated.
 * Auxiliary to 'a_jobcntl_update_and_notify'. */
ASHE_PRIVATE void a_jobcntl_update(struct a_jobcntl *jobcntl)
{
	int32 status, ret;
	pid pid;

	do {
		pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
		ret = a_jobcntl_update_process(jobcntl, pid, status);
		ashe_assert(ret >= 0);
	} while (ret > 0);
}

/* Updates the 'JobControl' by removing all of the finished jobs.
 * Additionally reports any of the jobs that were completed and/or stopped.
 * In most cases gets called inside the signal handler, so we have to take
 * care of the terminal input and about redrawing the screen properly.
 * Calling this directly without masking off the other signals with
 * SIG_BLOCK is highly unsafe and will probably break something. */
ASHE_PUBLIC void a_jobcntl_update_and_notify(struct a_jobcntl *jobcntl)
{
	struct a_term *term = &ashe.sh_term;
	uint32 col, row, idx;
	memmax jobcnt, i;
	struct a_job *job, out;
	ubyte completed;

	jobcnt = a_jobcntl_jobs(jobcntl);
	a_jobcntl_update(jobcntl);

	col = COL;
	row = ROW;
	idx = IBFIDX;

	for (i = 0; i < jobcnt;) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		completed = 0;

		if (a_job_is_completed(job)) {
			completed = 1;
			out = a_jobcntl_remove(jobcntl, i);
			job = &out;
			jobcnt--;
			if (!job->foreground) /* @?: Can it ever be in fg ? */
				goto l_redraw;
			/* Is this reachable ? */
			a_job_free(job);
			continue;
		} else if (a_job_is_stopped(job) && !job->notified) {
l_redraw:
			if (ashe.sh_term.tm_reading) { /* in signal handler ? */
				ashe_cursor_end();
				ashe_print("\r\n", stderr);
			}
			printf_info("job %d %s %s", job->pgid, job->input,
				    (completed ? "completed [+]" :
						 "stopped [-]"));
			prompt_print();
			if (term->tm_reading) { /* TODO: fix ? */
				COL = col;
				ROW = row;
				IBFIDX = idx;
				ashe_redraw();
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

/* Returns address of a Job that has a matching 'id' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL. */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_id(struct a_jobcntl *jobcntl,
						    memmax id)
{
	memmax jobcnt, i;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		if (job->id == id)
			return job;
	}
	return NULL;
}

/* Returns address of a Job that contains a process with matching
 * 'pid' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL. */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_pid(struct a_jobcntl *jobcntl,
						     pid pid)
{
	memmax jobcnt, proc_cnt, i, j;
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

/* Returns address of a Job that has a matching 'pgid' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL. */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_with_pgid(struct a_jobcntl *jobcntl,
						      pid pgid)
{
	memmax jobcnt, i;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = a_jobcntl_get_job_at(jobcntl, i);
		if (job->pgid == pgid)
			return job;
	}
	return NULL;
}

/* Gets the Job from 'jobcntl' that matches the 'where' flag.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where' is zero then only a background job can be returned.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out). */
ASHE_PUBLIC struct a_job *a_jobcntl_get_job_from(struct a_jobcntl *jobcntl,
						 ubyte where)
{
	memmax jobcnt;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);
	while (jobcnt--) {
		job = a_jobcntl_get_job_at(jobcntl, jobcnt);
		if (job->foreground == where)
			return job;
	}
	return NULL;
}

/* Gets the Job from 'jobcntl' that matches 'where' and has matching 'id'.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where is zero then only a background job can be returned.
 * Job additionally must have matching 'id'.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out). */
ASHE_PUBLIC struct a_job *
a_jobcntl_get_job_with_id_from(struct a_jobcntl *jobcntl, memmax id,
			       ubyte where)
{
	memmax jobcnt;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);
	while (jobcnt--) {
		job = a_jobcntl_get_job_at(jobcntl, jobcnt);
		if (job->id == id && job->foreground == where)
			return job;
	}
	return NULL;
}

/* Gets the Job from 'jobcntl' that matches 'where' and 'pid'.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where is zero then only a background job can be returned.
 * Job additionally must have matching 'pid'.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out). */
ASHE_PUBLIC struct a_job *
a_jobcntl_get_job_with_pid_from(struct a_jobcntl *jobcntl, pid pid, ubyte where)
{
	memmax jobcnt;
	struct a_job *job;

	jobcnt = a_jobcntl_jobs(jobcntl);
	while (jobcnt--) {
		job = a_jobcntl_get_job_at(jobcntl, jobcnt);
		if (job->foreground == where && a_job_contains_pid(job, pid))
			return job;
	}
	return NULL;
}

/* Gets the Job from 'jobcntl' that matches 'where' and has matching 'pgid'.
 * If 'where' is a non zero value then only foreground jobs can be returned.
 * If 'where is zero then only a background job can be returned.
 * Job additionally must have matching 'pgid'.
 * If no matching jobs were found, NULL is returned.
 * If the job is found it will be returned as in fifo (first in first out). */
ASHE_PUBLIC struct a_job *
a_jobcntl_get_job_with_pgid_from(struct a_jobcntl *jobcntl, pid pgid,
				 ubyte where)
{
	struct a_job *job;
	memmax jobcnt;

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
	pid pid;
	struct timespec ts;
	memmax zombies, processes;

	if (kill(-job->pgid, SIGKILL) < 0)
		ashe_perrno();

	ts.tv_sec = 0;
	ts.tv_nsec = (ASHE_WAIT_BEFORE_HARVEST_MS % 1000) * 1000000;
	nanosleep(&ts, NULL);

	zombies = 0;
	processes = a_job_processes(job);
	do { /* harvest */
		pid = waitpid(-job->pgid, NULL, WUNTRACED);
		if (pid == 0)
			zombies++;
		processes--;
	} while (pid != -1 && processes > 0);

	if (unlikely(zombies > 0))
		ashe_eprintf("was unable to reap %d processes in PGID %d",
			     zombies, job->pgid);
}

/* Kills and harvests all processes belonging
 * to any job inside of 'jobcntl'. */
ASHE_PUBLIC void a_jobcntl_harvest(struct a_jobcntl *jobcntl)
{
	memmax len;

	len = a_jobcntl_jobs(jobcntl);
	while (len--)
		a_job_kill_and_harvest(a_jobcntl_get_job_at(jobcntl, len));
}
