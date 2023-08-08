#define _GNU_SOURCE
#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"
#include "shell.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#undef joblist /* Don't need it here */

#define POLITE(status) (WTERMSIG((status)) & (SIGTERM | SIGINT | SIGQUIT | SIGKILL | SIGHUP))

#define job_completed_format green("completed") obrack red("-") cbrack
#define job_stopped_format   red("stopped") obrack magenta("!") cbrack
#define job_started_format   byellow("started") obrack green("+") cbrack
#define print_sigterm(status, polite)                                                          \
    process_format(                                                                            \
        proc,                                                                                  \
        "was " bold(bred("TERMINATED")) " by " bold(magenta("SIG")) bold(magenta("%s")) " %s", \
        sigabbrev_np(status),                                                                  \
        ((polite) ? " (" italic("polite") ")" : ""))

static size_t joblist_id(joblist_t *joblist);
static job_t *joblist_get_job(joblist_t *joblist, bool foreground);
static job_t *joblist_get_pid(joblist_t *joblist, pid_t pid, bool foreground);
static job_t *joblist_get_id(joblist_t *joblist, size_t id, bool foreground);
static bool job_contains_pid(job_t *job, pid_t pid);
static bool update_process(joblist_t *joblist, pid_t pid, int status);
static void process_format(process_t *p, byte *fmt, ...);
static void process_drop(process_t *process);
static int job_wait(job_t *job);
static void job_kill_and_harvest(job_t *job);

joblist_t joblist_init(void)
{
    return (joblist_t){
        .jobs = vec_new(sizeof(job_t)),
        .next_id = 1,
    };
}

static size_t joblist_id(joblist_t *joblist)
{
    if(joblist_len(joblist) == 0)
        joblist->next_id = 1;
    return joblist->next_id++;
}

bool joblist_push(joblist_t *joblist, job_t *job)
{
    job->id = joblist_id(joblist);
    return vec_push(joblist->jobs, job);
}

job_t *joblist_at(joblist_t *joblist, size_t i)
{
    return vec_index(joblist->jobs, i);
}

void joblist_remove(joblist_t *joblist, size_t i)
{
    vec_remove(joblist->jobs, i, (FreeFn) job_drop);
}

bool joblist_remove_job(joblist_t *joblist, job_t *job)
{
    size_t len = joblist_len(joblist);
    for(size_t i = 0; i < len; i++) {
        if(joblist_at(joblist, i)->id == job->id) {
            joblist_remove(joblist, i);
            return true;
        }
    }
    return false;
}

size_t joblist_len(joblist_t *jlist)
{
    return vec_len(jlist->jobs);
}

void joblist_update(joblist_t *joblist)
{
    int status;
    pid_t pid;

    do {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
    } while(update_process(joblist, pid, status));
}

void joblist_update_and_notify(__attribute__((unused)) int signum)
{
    size_t len = joblist_len(&shell.sh_jlist);
    job_t *job;

    joblist_update(&shell.sh_jlist);

    for(size_t i = 0; i < len;) {
        job = joblist_at(&shell.sh_jlist, i);

        if(job_completed(job)) {
            /// pprompt format depends on joblist len this is why
            /// we explicitly remove job in both branches ordering
            /// is important to print correct job len in the prompt
            if(!job->foreground) {
                ATOMIC_PRINT({
                    job_format(job, job_completed_format);
                    joblist_remove(&shell.sh_jlist, i);
                    pprompt();
                    if(shell.sh_term.tm_reading)
                        inbuff_redraw(&shell.sh_term.tm_inbuff);
                });
            } else {
                joblist_remove(&shell.sh_jlist, i);
            }

            len--;
            continue;
        } else if(job_stopped(job) && !job->notified) {
            ATOMIC_PRINT({
                job_format(job, job_stopped_format);
                pprompt();
                if(shell.sh_term.tm_reading)
                    inbuff_redraw(&shell.sh_term.tm_inbuff);
            });
            job->notified = true;
        }
        i++;
    }
}

static job_t *joblist_get_job(joblist_t *joblist, bool foreground)
{
    size_t len = joblist_len(joblist);
    job_t *job;

    while(len--) {
        job = joblist_at(joblist, len);
        if(job->foreground == foreground)
            return job;
    }

    return NULL;
}

static job_t *joblist_get_pid(joblist_t *joblist, pid_t pid, bool foreground)
{
    size_t len = joblist_len(joblist);
    job_t *job;

    while(len--) {
        job = joblist_at(joblist, len);
        if(job->foreground == foreground && job_contains_pid(job, pid))
            return job;
    }

    return NULL;
}

static job_t *joblist_get_id(joblist_t *joblist, size_t id, bool foreground)
{
    size_t len = joblist_len(joblist);
    job_t *job;

    while(len--) {
        job = joblist_at(joblist, len);
        if(job->id == id && job->foreground == foreground)
            return job;
    }

    return NULL;
}

static bool job_contains_pid(job_t *job, pid_t pid)
{
    size_t len = job_len(job);
    while(len--) {
        if(job_at(job, len)->pid == pid)
            return true;
    }
    return false;
}

job_t *joblist_find_id(joblist_t *joblist, size_t id)
{
    size_t len = joblist_len(joblist);
    job_t *job;

    for(size_t i = 0; i < len; i++) {
        job = joblist_at(joblist, i);
        if(job->id == id)
            return job;
    }

    return NULL;
}

job_t *joblist_find_pid(joblist_t *joblist, pid_t pid)
{
    size_t len = joblist_len(joblist);
    job_t *job;
    size_t jobn;

    for(size_t i = 0; i < len; i++) {
        job = joblist_at(joblist, i);
        jobn = job_len(job);

        for(size_t j = 0; j < jobn; j++)
            if(job_at(job, j)->pid == pid)
                return job;
    }

    return NULL;
}

/*********** For 'bg' builtin *************/

job_t *joblist_get_bg_pid(joblist_t *joblist, pid_t pid)
{
    return joblist_get_pid(joblist, pid, false);
}

job_t *joblist_get_bg_id(joblist_t *joblist, size_t id)
{
    return joblist_get_id(joblist, id, false);
}

job_t *joblist_get_bg_job(joblist_t *joblist)
{
    return joblist_get_job(joblist, false);
}

/******************************************/
/*
 *
 *
 */
/*********** For 'fg' builtin *************/

job_t *joblist_get_fg_pid(joblist_t *joblist, pid_t pid)
{
    return joblist_get_pid(joblist, pid, true);
}

job_t *joblist_get_fg_id(joblist_t *joblist, size_t id)
{
    return joblist_get_id(joblist, id, true);
}

job_t *joblist_get_fg_job(joblist_t *joblist)
{
    return joblist_get_job(joblist, true);
}

/******************************************/

/*
 *
 *
 *
 *
 */

/// ************** JOB ****************** ///
job_t job_new(byte connection, bool bg)
{
    return (job_t){
        .pgid = 0,
        .notified = false,
        .connection = connection,
        .foreground = !bg || connection & (JC_AND | JC_OR),
        .processes = vec_new(sizeof(process_t)),
        .id = 0,
        .tmodes = shell.sh_term.tm_dflterm,
    };
}
void joblist_drop(joblist_t *joblist)
{
    size_t len = joblist_len(joblist);
    job_t *job;

    while(len--) {
        job = joblist_at(joblist, len);
        job_kill_and_harvest(job);
    }

    vec_drop(&joblist->jobs, (FreeFn) job_drop);
}

void job_drop(job_t *job)
{
    vec_drop(&job->processes, (FreeFn) process_drop);
}

process_t *job_at(job_t *job, size_t i)
{
    return vec_index(job->processes, i);
}

void job_format(job_t *job, byte *fmt, ...)
{
    size_t len = job_len(job);
    va_list argp;

    va_start(argp, fmt);

    ATOMIC_PRINT({
        fprintf(stderr, "\r\n" obrack bold(byellow("J_%ld")) cbrack obrack blue("\""), job->id);
        for(size_t i = 0; i < len; i++)
            fprintf(stderr, bred("%s %s"), job_at(job, i)->commandline, (i + 1 == len) ? "" : "| ");
        fprintf(stderr, blue("\b\"") cbrack cyan(" -> "));
        vfprintf(stderr, fmt, argp);
        fprintf(stderr, "\r\n");
    });

    va_end(argp);
}

bool job_add_process(job_t *job, process_t *process)
{
    if(__glibc_unlikely(!vec_push(job->processes, process))) {
        ATOMIC_PRINT(PW_PROCTOJOB(process->pid));
        return false;
    }
    return true;
}

job_t *joblist_getjob(joblist_t *jlist, pid_t pgid)
{
    vec_t *list = jlist->jobs;
    size_t len = vec_len(list);
    job_t *job;
    for(size_t i = 0; i < len; i++)
        if((job = vec_index(list, i))->pgid == pgid)
            return job;
    return NULL;
}

bool job_stopped(job_t *job)
{
    size_t len = job_len(job);
    for(size_t i = 0; i < len; i++)
        if(job_at(job, i)->stopped)
            return true;
    return false;
}

bool job_completed(job_t *job)
{
    size_t len = job_len(job);
    for(size_t i = 0; i < len; i++)
        if(!job_at(job, i)->completed)
            return false;
    return true;
}

process_t *job_lastp(job_t *job)
{
    return vec_back(job->processes);
}

static int job_wait(job_t *job)
{
    int status;
    pid_t pid;

    do {
        pid = waitpid(-job->pgid, &status, WUNTRACED);
    } while(job_update(job, pid, status) && !job_stopped(job) && !job_completed(job));

    if(job_stopped(job))
        return 0;
    else
        return job_lastp(job)->status;
}

static void job_kill_and_harvest(job_t *job)
{
    pid_t pid;
    size_t zombies = 0;

    if(kill(-job->pgid, SIGKILL) < 0) {
        ATOMIC_PRINT({
            pwarn("failed sending a SIGKILL to process group ID:%d", job->pgid);
            perr();
        });
    }

    do {
        pid = waitpid(-job->pgid, NULL, WNOHANG);
        if(pid == 0)
            zombies++;
    } while(errno != ECHILD);

    if(__glibc_unlikely(zombies > 0))
        pwarn("%d zombie processes not reaped in the process group ID %d", zombies, job->pgid);
}

bool job_update(job_t *job, pid_t pid, int status)
{
    size_t len = job_len(job);
    process_t *proc = NULL;

    if(pid > 0) {
        for(size_t i = 0; i < len; i++) {
            proc = job_at(job, i);

            if(proc->pid == pid) {
                if(WIFSTOPPED(status)) {
                    proc->stopped = true;
                } else {
                    proc->completed = true;
                    if(WIFSIGNALED(status)) {
                        proc->status = WTERMSIG(status);
                    } else {
                        assert(WIFEXITED(status));
                        proc->status = WEXITSTATUS(status);
                    }
                }
                return true;
            }
        }
        /// Invariant broken
        pwarn(
            "FIX ME: foreground job was trying to update invalid process! PROCESS -> "
            "PID:%d, CMDLINE:'%s'\r\n",
            proc->pid,
            proc->commandline);
        return false;
    } else if(ECHILD == errno) {
        return false;
    } else {
        perr();
        return false;
    }
}

int job_move_to_fg(job_t *job, bool cont)
{
    /* Mark job as foreground */
    job->foreground = true;

    /* Put job process group into the foreground */
    tcsetpgrp(TERMINAL_FD, job->pgid); /* Can't fail */

    /* If continue flag is set, send SIGCONT signal to the job process group */
    if(cont) {
        if(__glibc_unlikely(
               tcsetattr(TERMINAL_FD, TCSADRAIN, &job->tmodes) < 0 || kill(-job->pgid, SIGCONT) < 0))
        {
            ATOMIC_PRINT(perr());
        }
    }

    /* Wait for job to either complete or get stopped */
    int status = job_wait(job);

    /* If job was originally ran in the foreground without being stopped prior
     * and now it is stopped, then move it into the joblist */
    if(__glibc_unlikely(!cont && job_stopped(job) && !joblist_push(&shell.sh_jlist, job))) {
        ATOMIC_PRINT(PW_ADDJ(job));
        exit(EXIT_FAILURE);
    }

    /* put shell back into the foreground */
    tcsetpgrp(TERMINAL_FD, getpgrp()); /* Can't fail */

    /* Update the terminal modes for the job and load the default shell terminal mode. */
    if(__glibc_unlikely(
           tcgetattr(TERMINAL_FD, &job->tmodes)
           || tcsetattr(TERMINAL_FD, TCSADRAIN, &shell.sh_term.tm_dflterm) < 0))
    {
        ATOMIC_PRINT({
            PW_SHDFLMODE;
            perr();
        });
    }

    return status;
}

void job_move_to_bg(job_t *job, bool cont)
{
    job->foreground = false;
    if(cont)
        if(__glibc_unlikely(kill(-job->pgid, SIGCONT) < 0))
            ATOMIC_PRINT(perr(););
}

size_t job_len(job_t *job)
{
    return vec_len(job->processes);
}

void job_sa_running(job_t *job)
{
    size_t len = job_len(job);
    for(size_t i = 0; i < len; i++)
        job_at(job, i)->stopped = false;
    job->notified = 0;
}

void job_continue(job_t *job, bool foreground)
{
    job_sa_running(job);
    if(foreground) {
        job_move_to_fg(job, true);
        if(__glibc_unlikely(job_completed(job) && !joblist_remove_job(&shell.sh_jlist, job))) {
            /// Invariant broken
            ATOMIC_PRINT(pwarn("FIX ME: tried removing background job that was not in the joblist!"));
            exit(EXIT_FAILURE);
        }
    } else
        job_move_to_bg(job, true);
}

process_t process_new(pid_t pid, byte *argv)
{
    return (process_t){
        .status = 0,
        .stopped = false,
        .completed = false,
        .pid = pid,
        .commandline = argv,
    };
}

static void process_drop(process_t *process)
{
    free(process->commandline);
}

static void process_format(process_t *p, byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);

    ATOMIC_PRINT({
        fprintf(
            stderr,
            "\r\n" obrack bold(blue("P_%d")) cbrack obrack blue("\"") bred("%s") blue("\"")
                cbrack cyan(" -> "),
            p->pid,
            p->commandline);
        vfprintf(stderr, fmt, argp);
        fprintf(stderr, "\r\n");
    });

    va_end(argp);
}

static bool update_process(joblist_t *joblist, pid_t pid, int status)
{
    size_t joblistn = joblist_len(joblist);
    process_t *proc = NULL;

    if(pid > 0) {
        for(size_t i = 0; i < joblistn; i++) {
            job_t *job = joblist_at(joblist, i);
            size_t jobn = job_len(job);

            for(size_t j = 0; j < jobn; j++) {
                proc = job_at(job, j);

                if(proc->pid == pid) {
                    proc->status = status;
                    if(WIFSTOPPED(status)) {
                        proc->stopped = true;
                    } else {
                        proc->completed = true;
                        if(WIFSIGNALED(status)) {
                            proc->status = WTERMSIG(status);
                            ATOMIC_PRINT(print_sigterm(proc->status, POLITE(status)));
                        } else if(WIFEXITED(status)) {
                            proc->status = WEXITSTATUS(status);
                        }
                    }
                    return true;
                }
            }
        }
        pwarn(
            "FIX ME: tried to update process that is not inside the joblist! PROCESS -> "
            "PID:%d, CMDLINE:'%s'",
            proc->pid,
            proc->commandline);
        return false;
    } else if(pid == 0 || errno == ECHILD) {
        return false;
    } else {
        ATOMIC_PRINT({ perr(); });
        return false;
    }

    return false;
}
