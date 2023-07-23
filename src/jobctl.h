#ifndef __ASH_JOBCTL__H__
#define __ASH_JOBCTL__H__

#include "ashe_utils.h"
#include "parser.h"
#include "vec.h"

#include <signal.h>
#include <sys/types.h>
#include <termios.h>

typedef struct _process_t {
  pid_t pid;
  int status;
  bool stopped;
  bool completed;
  byte *commandline;
} process_t;

typedef struct joblist_t joblist_t;

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

/// Job connection
#define JC_AND ASH_AND /* '&&' */
#define JC_OR ASH_OR   /* '||' */
#define JC_NONE ASH_NONE

/// JOBLIST
bool joblist_init();
void joblist_drop(void);
void joblist_update_and_notify(int signum);
bool joblist_push(job_t *job);
job_t *joblist_last(void);
job_t *joblist_getjob(joblist_t *jlist, pid_t pgid);

/// JOB
job_t job_new(byte connection, bool bg);
bool job_add_process(job_t *job, process_t *process);
int job_move_to_fg(job_t *job, bool cont);
void job_move_to_bg(job_t *job, bool cont);
void job_format(job_t *job, byte *fmt, ...);
void job_continue(job_t *job, bool foreground);
void job_drop(job_t *job);

/// PROCESS
process_t process_new(pid_t pid, byte *argv);

#endif
