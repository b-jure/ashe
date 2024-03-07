#ifndef __ASH_JOBCTL__H__
#define __ASH_JOBCTL__H__


#include "acommon.h"
#include "aarray.h"
#include "token.h"

#include <termios.h>


typedef struct {
    pid pid; /* process ID */
    int32 status; /* exit status */
    ubyte stopped; /* flag indicating if process is stopped */
    ubyte completed; /* flag indicating if process is finished executing */
    Buffer cmd; /* @?: debug */
} Process;

void Process_init(Process* proc, pid pid, byte* argv);
void Process_free(Process* proc);



/* Array of 'Process's */
ARRAY_NEW(ArrayProcess, Process);
typedef struct {
    ArrayProcess processes;
    pid pgid; /* process group ID */
    ubyte notified; /* @? */
    ubyte foreground; /* flag indicating if the job is running in foreground */
    Connection connection; /* connection type */
    memmax id;
    struct termios tmodes;
} Job;

void Job_init(Job* job, ubyte con, ubyte bg);
Process* Job_get_process(Job* job, memmax i);
memmax Job_process_cnt(Job* job);
ubyte Job_add_process(Job* job, Process* process);
ubyte Job_isstopped(Job* job);
ubyte Job_iscompleted(Job* job);
void Job_move_to_bg(Job* job, ubyte cont);
int32 Job_move_to_fg(Job* job, ubyte cont);
void Job_format(Job* job, byte* fmt, ...);
void Job_continue(Job* job, ubyte foreground);
ubyte Job_update_process(Job* job, pid pid, int32 status);
void Job_free(Job* job); 



/* Array of 'Job's */
ARRAY_NEW(ArrayJob, Job);
typedef struct {
    ArrayJob jobs; /* running/stopped (unfinished) jobs */
} Joblist; // wrapper type for interface

void Joblist_init(Joblist* jlist);
#define Joblist_get(joblist, i) ArrayJob_index(&(joblist)->jobs, i)
ubyte Joblist_remove_job(Joblist* jlist, Job* job);
void Joblist_drop(Joblist* jlist);
void Joblist_update_and_notify(Joblist* joblist, int32 signum); /* Signal handler */
ubyte Joblist_push(Joblist* jlist, Job* job);
Job* Joblist_getjob(Joblist* jlist, pid pgid);
Job* Joblist_find_pid(Joblist* jlist, pid pid);
Job* Joblist_find_id(Joblist* jlist, memmax id);
Job* Joblist_get_fg_job(Joblist* jlist);
Job* Joblist_get_fg_pid(Joblist* jlist, pid pid);
Job* Joblist_get_fg_id(Joblist* jlist, memmax id);
Job* Joblist_get_bg_job(Joblist* jlist);
Job* Joblist_get_bg_pid(Joblist* jlist, pid pid);
Job* Joblist_get_bg_id(Joblist* jlist, memmax id);


#endif
