#ifndef AJOBCNTL_H
#define AJOBCNTL_H

#include "acommon.h"
#include "aarray.h"
#include "atoken.h"

#include <termios.h>

typedef struct {
	pid pid; /* process ID */
	int32 status; /* exit status */
	ubyte stopped; /* flag indicating if process is stopped */
	ubyte completed; /* flag indicating if process is finished executing */
} Process;

ARRAY_NEW(ArrayProcess, Process)

void Process_init(Process *proc, pid pid);

typedef struct {
	struct termios tmodes; /* Terminal attributes/settings */
	ArrayProcess processes; /* processes belonging to the job */
	memmax id; /* job id, ordered (1,2,3...) */
	pid pgid; /* process group ID */
	ubyte notified; /* @? */
	ubyte foreground; /* set if job is running in foreground */
	const char *input; /* input from terminal (debug) */
} Job;

ARRAY_NEW(ArrayJob, Job)

void Job_init(Job *job, const char *dbginput, ubyte isbg);
memmax Job_processes(Job *job);
void Job_add_process(Job *job, Process process);
ubyte Job_is_stopped(Job *job);
ubyte Job_is_completed(Job *job);
void Job_mark_as_background(Job *job, ubyte cont);
int32 Job_move_to_foreground(Job *job, ubyte cont);
void Job_continue(Job *job, ubyte isfg);
void Job_free(Job *job);

/* ===== Interface to job control ==== */

typedef struct {
	ArrayJob jobs; /* running/stopped jobs */
} JobControl; // wrapper type for job control interface

void JobControl_init(JobControl *jobcntl);

memmax JobControl_jobs(JobControl *jobcntl);
Job *JobControl_get_job_at(JobControl *jobcntl, uint32 i);
void JobControl_add_job(JobControl *jobcntl, Job *job);
ubyte JobControl_remove_job(JobControl *jobcntl, Job *job);
void JobControl_update_and_notify(JobControl *jobcntl);

Job *JobControl_get_job_with_id(JobControl *jobcntl, memmax id);
Job *JobControl_get_job_with_pid(JobControl *jobcntl, pid pid);
Job *JobControl_get_job_with_pgid(JobControl *jobcntl, pid gpid);

Job *JobControl_get_job_from(JobControl *jobcntl, ubyte where);
Job *JobControl_get_job_with_id_from(JobControl *jobcntl, memmax id,
				     ubyte where);
Job *JobControl_get_job_with_pid_from(JobControl *jobcntl, pid pid,
				      ubyte where);
Job *JobControl_get_job_with_pgid_from(JobControl *jobcntl, pid pgid,
				       ubyte where);

Job *JobControl_get_job_with_id(JobControl *jobcntl, memmax id);
Job *JobControl_get_job_with_pid(JobControl *jobcntl, pid pid);
Job *JobControl_get_job_with_pgid(JobControl *jobcntl, pid pgid);
Job *JobControl_get_foreground_job(JobControl *jobcntl);

void JobControl_harvest(JobControl *jobcntl);
void JobControl_free(JobControl *jobcntl);

#endif
