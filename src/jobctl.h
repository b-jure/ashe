#ifndef __ASH_JOBCTL__H__
#define __ASH_JOBCTL__H__

#include "ashe_utils.h"
#include "parser.h"
#include "vec.h"

#include <signal.h>
#include <termios.h>

typedef struct _process_t {
  pid_t pid;
  int status;
  bool stopped;
  bool completed;
  byte *commandline;
} process_t;

typedef struct joblist_t {
  vec_t *jobs;
  size_t next_id;
} joblist_t;

extern joblist_t joblist;

typedef struct {
  vec_t *processes;
  pid_t pgid;
  bool notified;
  bool foreground;
  byte connection;
  size_t id;
  struct termios tmodes;
} job_t;

/// Job connection (conditionals)
#define JC_AND ASH_AND   /* '&&' */
#define JC_OR ASH_OR     /* '||' */
#define JC_NONE ASH_NONE /* No connection */

/// JOBLIST
size_t joblist_len(void);
job_t *joblist_at(size_t i);
bool joblist_remove_job(job_t *job);
bool joblist_init();
void joblist_drop(void);
void joblist_update_and_notify(int signum);
bool joblist_push(job_t *job);
job_t *joblist_last(void);
job_t *joblist_getjob(joblist_t *jlist, pid_t pgid);
job_t *joblist_find_pid(pid_t pid);
job_t *joblist_find_id(size_t id);
job_t *joblist_get_fg_job(void);
job_t *joblist_get_bg_job(void);

/// JOB
job_t job_new(byte connection, bool bg);
process_t *job_at(job_t *job, size_t i);
size_t job_len(job_t *job);
bool job_add_process(job_t *job, process_t *process);
int job_move_to_fg(job_t *job, bool cont);
bool job_stopped(job_t *job);
bool job_completed(job_t *job);
void job_move_to_bg(job_t *job, bool cont);
void job_format(job_t *job, byte *fmt, ...);
void job_continue(job_t *job, bool foreground);
void job_drop(job_t *job);
bool job_update(job_t *job, pid_t pid, int status);

/// PROCESS
process_t process_new(pid_t pid, byte *argv);
void process_drop(process_t *process);

#endif
