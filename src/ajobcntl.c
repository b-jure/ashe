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
ASHE_PUBLIC void Process_init(Process *proc, pid PID)
{
	proc->pid = PID;
	proc->status = 0;
	proc->stopped = 0;
	proc->completed = 0;
}

/* ==== JOB ==== */

/* Initialize the 'job', store 'input' for debug and
 * 'bg' is a flag indicating if this job is running in background. */
ASHE_PUBLIC void Job_init(Job *job, const char *input, ubyte bg)
{
	job->tmodes = ashe.sh_term.tm_dfltermios;
	ArrayProcess_init(&job->processes);
	job->id = 0;
	job->pgid = 0;
	job->notified = 0;
	job->foreground = !bg;
	job->input = input;
}

/* Gets process at 'i'. */
ASHE_PRIVATE inline Process *Job_get_process(Job *job, memmax i)
{
	return ArrayProcess_index(&job->processes, i);
}

/* Returns 1 if 'job' contains process with 'pid', 0 otherwise. */
ASHE_PRIVATE inline ubyte Job_contains_pid(Job *job, pid pid)
{
	memmax jobcnt;

	jobcnt = Job_processes(job);
	while (jobcnt--)
		if (Job_get_process(job, jobcnt)->pid == pid)
			return 1;
	return 0;
}

/* Returns the number of processes currently in the 'job'. */
ASHE_PUBLIC memmax Job_processes(Job *job)
{
	return ArrayProcess_len(&job->processes);
}

/* Add 'process' into 'job'. */
ASHE_PUBLIC void Job_add_process(Job *job, Process process)
{
	ArrayProcess_push(&job->processes, process);
}

/* Return 1 if 'job' is stopped, otherwise 0. */
ASHE_PUBLIC ubyte Job_is_stopped(Job *job)
{
	memmax proc_cnt, i;

	proc_cnt = Job_processes(job);
	for (i = 0; i < proc_cnt; i++)
		if (Job_get_process(job, i)->stopped)
			return 1;
	return 0;
}

/* Return 1 if 'job' is completed, otherwise 0. */
ASHE_PUBLIC ubyte Job_is_completed(Job *job)
{
	memmax proc_cnt, i;

	proc_cnt = Job_processes(job);
	for (i = 0; i < proc_cnt; i++)
		if (!Job_get_process(job, i)->completed)
			return 0;
	return 1;
}

/* Marks the 'job' as running in the background.
 * If 'cont' is non zero then SIGCONT signal is
 * sent to the job's process group ID. */
ASHE_PUBLIC void Job_mark_as_background(Job *job, ubyte cont)
{
	job->foreground = 0;
	if (unlikely(cont && kill(-job->pgid, SIGCONT) < 0))
		ashe_perrno();
}

/* Return string representation of 'sig'.
 * This function only handles signals specified by ISO C.
 * Auxiliary to 'JobControl_update_process()'. */
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
ASHE_PRIVATE void proc_term_notify(Process *proc, int32 status, ubyte notify)
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

ASHE_PRIVATE void Process_update_status(Process *proc, int32 status,
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
ASHE_PRIVATE Process *Job_update_process_status(Job *job, pid PID, int32 status)
{
	Process *proc;
	memmax i, proc_cnt;

	ashe_assert(PID > 0);
	proc_cnt = Job_processes(job);
	for (i = 0; i < proc_cnt; i++) {
		proc = Job_get_process(job, i);
		if (proc->pid == PID) {
			Process_update_status(proc, status, !job->foreground);
			return proc;
		}
	}
	return NULL;
}

/* Waits for the 'job' to finish or until it gets paused.
 * Auxiliary to 'Job_move_to_foreground()'. */
ASHE_PRIVATE int32 Job_wait(Job *job, ubyte *stop)
{
	pid PID;
	int32 status;
	Process *proc;

	do {
		errno = 0;
		PID = waitpid(-job->pgid, &status, WUNTRACED);
		if (unlikely(PID < 0)) {
			ashe_perrno();
			panic(NULL);
		}
		proc = Job_update_process_status(job, PID, status);
		ashe_assert(proc != NULL);
	} while (!proc->stopped && !(proc->stopped = Job_is_stopped(job)) &&
		 !Job_is_completed(job));

	*stop = proc->stopped;
	return ArrayProcess_last(&job->processes)->status;
}

/* Moves the 'job' into foreground.
 * If 'cont' is non zero then SIGCONT signal
 * is sent to the 'job's process group ID. */
ASHE_PUBLIC int32 Job_move_to_foreground(Job *job, ubyte cont, ubyte *stop)
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
	int32 status = Job_wait(job, stop);
	/* 'cont' means the 'job' is already inside of the 'JobControl' */
	if (!cont && *stop)
		JobControl_add_job(&ashe.sh_jobcntl, job);
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
		Job_free(job);
	panic(NULL);
	return 0; /* UNREACHED */
}

/* Set 'job' as running by setting all processes that
 * belong to the 'job' as not stopped.
 * Auxiliary to 'Job_continue()'. */
ASHE_PRIVATE inline void Job_set_as_running(Job *job)
{
	memmax i, len;

	len = Job_processes(job);
	for (i = 0; i < len; i++)
		Job_get_process(job, i)->stopped = 0;
	job->notified = 0;
}

/* Sets the 'job' as running and if 'isfg' is a non zero
 * value then the job is moved into foreground.
 * If 'isfg' is 0, then the job is not moved into foreground
 * and is marked as background.
 * Additionally SIGCONT signal is sent to the job's process
 * group ID in case the 'job' was stopped. */
ASHE_PUBLIC void Job_continue(Job *job, ubyte isfg)
{
	ubyte stopped = 0;

	Job_set_as_running(job);
	if (isfg) {
		Job_move_to_foreground(job, 1, &stopped);
		if (unlikely(!stopped && !JobControl_remove_job(
						 &ashe.sh_jobcntl, job, NULL)))
			panic("Couldn't remove the job from 'JobControl'.");
	} else {
		Job_mark_as_background(job, 1);
	}
}

/* Frees the 'job' resources. */
ASHE_PUBLIC void Job_free(Job *job)
{
	ArrayProcess_free(&job->processes, NULL);
	afree((void *)job->input);
}

/* ==== JOB-CONTROL ==== */

/* Initialize the 'jobcntl'. */
ASHE_PUBLIC void JobControl_init(JobControl *jobcntl)
{
	ArrayJob_init(&jobcntl->jobs);
}

/* Return the number of jobs in 'jobcntl'. */
ASHE_PUBLIC memmax JobControl_jobs(JobControl *jobcntl)
{
	return ArrayJob_len(&jobcntl->jobs);
}

/* Generate new id.
 * Auxiliary to 'JobControl_add_job()'. */
ASHE_PRIVATE inline memmax Joblist_id(JobControl *jobcntl)
{
	static int32 id = 1;

	if (jobcntl->jobs.len == 0)
		id = 1;
	return id++;
}

/* Add 'job' to 'jobcntl' and assign it new id. */
ASHE_PUBLIC void JobControl_add_job(JobControl *jobcntl, Job *job)
{
	job->id = Joblist_id(jobcntl);
	ArrayJob_push(&jobcntl->jobs, *job);
}

/* Remove 'Job' from 'jobcntl' located at index 'i'.
 * Auxiliary to 'JobControl_remove_job()'. */
ASHE_PRIVATE inline Job JobControl_remove(JobControl *jobcntl, uint32 i)
{
	return ArrayJob_remove(&jobcntl->jobs, i);
}

/* Getter, returns Job at 'i' located in 'jobcntl'. */
ASHE_PUBLIC Job *JobControl_get_job_at(JobControl *jobcntl, uint32 i)
{
	return ArrayJob_index(&jobcntl->jobs, i);
}

/* Remove 'job' from 'jobcntl'.
 * If 'job' was found, remove it and free its resources, this returns 1.
 * Return 0 if the job was not found. */
ASHE_PUBLIC ubyte JobControl_remove_job(JobControl *jobcntl, Job *job, Job *out)
{
	memmax jobcnt, i;

	jobcnt = JobControl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		if (JobControl_get_job_at(jobcntl, i)->pgid == job->pgid) {
			if (out != NULL)
				*out = JobControl_remove(jobcntl, i);
			return 1;
		}
	}
	return 0;
}
/* Update process corresponding to 'pid' with 'status'.
 * Additionally report if the process was terminated by a signal.
 * Auxiliary to 'JobControl_update()'. */
ASHE_PRIVATE int32 JobControl_update_process(JobControl *jobcntl, pid pid,
					     int32 status)
{
	memmax jobcnt, i;
	Job *job;

	if (pid <= 0) {
		if (pid != 0 && errno != ECHILD)
			ashe_perrno();
		return 0;
	}

	jobcnt = JobControl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = JobControl_get_job_at(jobcntl, i);
		if (Job_update_process_status(job, pid, status) != NULL)
			return 1;
	}
	return -1;
}

/* Wait for any child process to stop or terminate and update it.
 * This runs until there are no any process that stopped or were terminated.
 * Auxiliary to 'JobControl_update_and_notify'. */
ASHE_PRIVATE void JobControl_update(JobControl *jobcntl)
{
	int32 status, ret;
	pid pid;

	do {
		pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
		ret = JobControl_update_process(jobcntl, pid, status);
		ashe_assert(ret >= 0);
	} while (ret > 0);
}

/* Updates the 'JobControl' by removing all of the finished jobs.
 * Additionally reports any of the jobs that were completed and/or stopped.
 * In most cases gets called inside the signal handler, so we have to take
 * care of the terminal input and about redrawing the screen properly.
 * Calling this directly without masking off the other signals with
 * SIG_BLOCK is highly unsafe and will probably break something. */
ASHE_PUBLIC void JobControl_update_and_notify(JobControl *jobcntl)
{
	Terminal *term = &ashe.sh_term;
	uint32 col, row;
	memmax jobcnt, i;
	Job *job, out;
	ubyte completed;

	jobcnt = JobControl_jobs(jobcntl);
	JobControl_update(jobcntl);

	col = COL;
	row = ROW;

	for (i = 0; i < jobcnt;) {
		job = JobControl_get_job_at(jobcntl, i);
		completed = 0;

		if (Job_is_completed(job)) {
			completed = 1;
			out = JobControl_remove(jobcntl, i);
			job = &out;
			jobcnt--;
			if (!job->foreground) /* @?: Can it ever be in fg ? */
				goto l_redraw;
			/* Is this reachable ? */
			Job_free(job);
			continue;
		} else if (Job_is_stopped(job) && !job->notified) {
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
				ashe_redraw();
			}
			if (completed) {
				Job_free(job);
				continue;
			}
			job->notified = 1;
		}
		i++;
	}
}

/* Returns address of a Job that has a matching 'id' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL. */
ASHE_PUBLIC Job *JobControl_get_job_with_id(JobControl *jobcntl, memmax id)
{
	memmax jobcnt, i;
	Job *job;

	jobcnt = JobControl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = JobControl_get_job_at(jobcntl, i);
		if (job->id == id)
			return job;
	}
	return NULL;
}

/* Returns address of a Job that contains a process with matching
 * 'pid' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL. */
ASHE_PUBLIC Job *JobControl_get_job_with_pid(JobControl *jobcntl, pid pid)
{
	memmax jobcnt, proc_cnt, i, j;
	Job *job;

	jobcnt = JobControl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = JobControl_get_job_at(jobcntl, i);
		proc_cnt = Job_processes(job);
		for (j = 0; j < proc_cnt; j++)
			if (Job_get_process(job, j)->pid == pid)
				return job;
	}
	return NULL;
}

/* Returns address of a Job that has a matching 'pgid' inside of 'jobcntl'.
 * If no matching Job was found this returns NULL. */
ASHE_PUBLIC Job *JobControl_get_job_with_pgid(JobControl *jobcntl, pid pgid)
{
	memmax jobcnt, i;
	Job *job;

	jobcnt = JobControl_jobs(jobcntl);
	for (i = 0; i < jobcnt; i++) {
		job = JobControl_get_job_at(jobcntl, i);
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
ASHE_PUBLIC Job *JobControl_get_job_from(JobControl *jobcntl, ubyte where)
{
	memmax jobcnt;
	Job *job;

	jobcnt = JobControl_jobs(jobcntl);
	while (jobcnt--) {
		job = JobControl_get_job_at(jobcntl, jobcnt);
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
ASHE_PUBLIC Job *JobControl_get_job_with_id_from(JobControl *jobcntl, memmax id,
						 ubyte where)
{
	memmax jobcnt;
	Job *job;

	jobcnt = JobControl_jobs(jobcntl);
	while (jobcnt--) {
		job = JobControl_get_job_at(jobcntl, jobcnt);
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
ASHE_PUBLIC Job *JobControl_get_job_with_pid_from(JobControl *jobcntl, pid pid,
						  ubyte where)
{
	memmax jobcnt;
	Job *job;

	jobcnt = JobControl_jobs(jobcntl);
	while (jobcnt--) {
		job = JobControl_get_job_at(jobcntl, jobcnt);
		if (job->foreground == where && Job_contains_pid(job, pid))
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
ASHE_PUBLIC Job *JobControl_get_job_with_pgid_from(JobControl *jobcntl,
						   pid pgid, ubyte where)
{
	Job *job;
	memmax jobcnt;

	jobcnt = JobControl_jobs(jobcntl);
	while (jobcnt--) {
		job = JobControl_get_job_at(jobcntl, jobcnt);
		if (job->pgid == pgid && job->foreground == where)
			return job;
	}
	return NULL;
}

/* Frees the memory occupied by 'jobcntl'. */
ASHE_PUBLIC void JobControl_free(JobControl *jobcntl)
{
	ArrayJob_free(&jobcntl->jobs, (FreeFn)Job_free);
}

/* Sends SIGKILL signal to all the processes that belong to 'job'. */
ASHE_PRIVATE void Job_kill_and_harvest(Job *job)
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
	processes = Job_processes(job);
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
ASHE_PUBLIC void JobControl_harvest(JobControl *jobcntl)
{
	memmax len;

	len = JobControl_jobs(jobcntl);
	while (len--)
		Job_kill_and_harvest(JobControl_get_job_at(jobcntl, len));
}
