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
    Buffer cmd; /* @?: debug */
} Process;

ARRAY_NEW(ArrayProcess, Process);

void Process_init(Process* proc, pid pid, byte* argv);
void Process_free(Process* proc);





typedef struct {
    ArrayProcess processes;
    pid pgid; /* process group ID */
    ubyte notified; /* @? */
    ubyte foreground; /* flag indicating if the job is running in foreground */
    Connection connection; /* connection type */
    memmax id;
    struct termios tmodes;
} Job;

ARRAY_NEW(ArrayJob, Job);

void Job_init(Job* job, ubyte con, ubyte bg);
Process* Job_get_process(Job* job, memmax i);
memmax Job_proccesses(Job* job);
ubyte Job_add_process(Job* job, Process* process);
ubyte Job_is_stopped(Job* job);
ubyte Job_is_completed(Job* job);
void Job_move_to_background(Job* job, ubyte cont);
int32 Job_move_to_foreground(Job* job, ubyte cont);
void Job_format(Job* job, byte* fmt, ...); /* ??? */
void Job_continue(Job* job, ubyte foreground);
ubyte Job_update_process(Job* job, pid pid, int32 status);
void Job_free(Job* job); 





typedef struct {
    ArrayJob jobs; /* running/stopped jobs */
} JobControl; // wrapper type for interface

/* Interface to job control */
void JobControl_init(JobControl* jobcntl);
memmax JobControl_jobs(JobControl* jobcntl);
#define JobControl_get_job(jobcntl, i) ArrayJob_index(&(jobcntl)->jobs, i)
ubyte JobControl_remove_job(JobControl* jobcntl, Job* job);
void JobControl_update_and_notify(JobControl* jobcntl, int32 signum); /* Signal handler */
ubyte JobControl_push(JobControl* jobcntl, Job* job);
Job* JobControl_pgid_find_job(JobControl* jobcntl, pid pgid);
Job* JobControl_pid_find_job(JobControl* jobcntl, pid pid);
Job* JobControl_id_find_job(JobControl* jobcntl, memmax id);
Job* JobControl_get_foreground_job(JobControl* jobcntl);
Job* JobControl_pid_get_foreground_job(JobControl* jobcntl, pid pid);
Job* JobControl_id_get_foreground_job(JobControl* jobcntl, memmax id);
Job* JobControl_get_background_job(JobControl* jobcntl);
Job* JobControl_pid_get_background_job(JobControl* jobcntl, pid pid);
Job* JobControl_id_get_background_job(JobControl* jobcntl, memmax id);
void JobControl_free(JobControl* jobcntl);

#endif
