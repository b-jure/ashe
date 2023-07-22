#ifndef __ASH_JOB_H__
#define __ASH_JOB_H__

#define _DEFAULT_SOURCE

#include "ashe_utils.h"
#include "parser.h"
#include "vec.h"

#include <signal.h>
#include <termios.h>

typedef struct {
  vec_t *jobs;
} joblist_t;

extern joblist_t joblist;

typedef struct {
  vec_t *processes;
  pid_t pgid;
  bool notified;
  bool foreground;
  byte connection;
  struct termios tmodes;
} job_t;

/// Job connection
#define JC_AND ASH_AND /* '&&' */
#define JC_OR ASH_OR   /* '||' */
#define JC_NONE ASH_NONE

typedef struct {
  pid_t pid;
  int status;
  bool stopped;
  bool completed;
  byte *commandline;
} process_t;

job_t job_new(byte connection, bool bg);
job_t *joblist_getjob(joblist_t *jlist, pid_t pgid);
bool job_add_process(job_t *job, process_t *process);
bool job_stopped(job_t *job);
bool job_completed(job_t *job);
size_t job_procn(job_t *job);
size_t joblist_len(void);
void joblist_killall(void);
void joblist_cleanup(void);
void job_move_to_fg(job_t *job, bool cont);
void job_move_to_bg(job_t *job, bool cont);
void process_drop(process_t *process);
void job_drop(job_t *job);
process_t process_new(pid_t pid);

#endif
