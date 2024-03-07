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



void Joblist_init(Joblist* joblist)
{
    ArrayJob_init(&joblist->jobs);
}

static memmax Joblist_id(Joblist* joblist)
{
    static int32 id = 1;
    if(joblist->jobs.len == 0) id = 1;
    return id++;
}

ubyte joblist_push(Joblist* joblist, Job* job)
{
    job->id = Joblist_id(joblist);
    return ArrayJob_push(&joblist->jobs, *job);
}

static finline void joblist_remove(Joblist* joblist, memmax i)
{
    Job job = ArrayJob_remove(&joblist->jobs, i);
    Job_free(&job);
}

ubyte joblist_remove_job(Joblist* joblist, Job* job)
{
    memmax len = joblist_len(joblist);
    for(memmax i = 0; i < len; i++) {
        if(Joblist_get_job(joblist, i)->id == job->id) {
            joblist_remove(joblist, i);
            return true;
        }
    }
    return false;
}

memmax joblist_len(Joblist* jlist)
{
    return vec_len(jlist->jobs);
}

void joblist_update(Joblist* joblist)
{
    int32   status;
    pid pid;

    do {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
    } while(update_process(joblist, pid, status));
}

void Joblist_update_and_notify(Joblist* joblist, int32 signum)
{
    unused(signum);
    memmax len = joblist_len(joblist);
    Job* job;

    joblist_update(joblist);

    for(memmax i = 0; i < len;) {
        job = Joblist_get_job(joblist, i);

        /* Cache positions and old prompt length */
        uint32 col  = inbuff.in_cur.cr_col;
        uint32 row  = inbuff.in_cur.cr_row;
        uint32 tcol = terminal.tm_col;
        uint32 plen = terminal.tm_plen;

        /// TODO: less repeats refactor the control flow
        if(job_completed(job)) {
            if(!job->foreground) {
                if(ashe.sh_term.tm_reading) {
                    inbuff_goto_end(&inbuff);
                }

                job_format(job, job_completed_format);
                joblist_remove(joblist, i);
                pprompt();

                if(ashe.sh_term.tm_reading) {
                    inbuff.in_cur.cr_col = col;
                    inbuff.in_cur.cr_row = row;
                    if(plen >= terminal.tm_plen) {
                        terminal.tm_col = tcol - (plen - terminal.tm_plen);
                    } else {
                        terminal.tm_col = tcol + (terminal.tm_plen - plen);
                    }
                    inbuff_redraw(&inbuff);
                }
            } else {
                joblist_remove(joblist, i);
            }

            len--;
            continue;
        } else if(job_stopped(job) && !job->notified) {
            if(ashe.sh_term.tm_reading) {
                inbuff_goto_end(&inbuff);
            }

            job_format(job, job_stopped_format);
            pprompt();

            if(ashe.sh_term.tm_reading) {
                inbuff.in_cur.cr_col = col;
                inbuff.in_cur.cr_row = row;
                if(plen >= terminal.tm_plen) {
                    terminal.tm_col = tcol - (plen - terminal.tm_plen);
                } else {
                    terminal.tm_col = tcol + (terminal.tm_plen - plen);
                }
                inbuff_redraw(&inbuff);
            }
            job->notified = true;
        }
        i++;
    }
}

static Job* joblist_get_job(Joblist* joblist, ubyte foreground)
{
    memmax len = joblist_len(joblist);
    Job* job;

    while(len--) {
        job = Joblist_get_job(joblist, len);
        if(job->foreground == foreground) return job;
    }

    return NULL;
}

static Job* joblist_get_pid(Joblist* joblist, pid pid, ubyte foreground)
{
    memmax len = joblist_len(joblist);
    Job* job;

    while(len--) {
        job = Joblist_get_job(joblist, len);
        if(job->foreground == foreground && job_contains_pid(job, pid)) return job;
    }

    return NULL;
}

static Job* joblist_get_id(Joblist* joblist, memmax id, ubyte foreground)
{
    memmax len = joblist_len(joblist);
    Job* job;

    while(len--) {
        job = Joblist_get_job(joblist, len);
        if(job->id == id && job->foreground == foreground) return job;
    }

    return NULL;
}

static ubyte job_contains_pid(Job* job, pid pid)
{
    memmax len = job_len(job);
    while(len--) {
        if(job_at(job, len)->pid == pid) return true;
    }
    return false;
}

Job* joblist_find_id(Joblist* joblist, memmax id)
{
    memmax len = joblist_len(joblist);
    Job* job;

    for(memmax i = 0; i < len; i++) {
        job = Joblist_get_job(joblist, i);
        if(job->id == id) return job;
    }

    return NULL;
}

Job* joblist_find_pid(Joblist* joblist, pid pid)
{
    memmax len = joblist_len(joblist);
    Job* job;
    memmax jobn;

    for(memmax i = 0; i < len; i++) {
        job  = Joblist_get_job(joblist, i);
        jobn = job_len(job);

        for(memmax j = 0; j < jobn; j++)
            if(job_at(job, j)->pid == pid) return job;
    }

    return NULL;
}

Job* joblist_get_bg_pid(Joblist* joblist, pid pid)
{
    return joblist_get_pid(joblist, pid, false);
}

Job* joblist_get_bg_id(Joblist* joblist, memmax id)
{
    return joblist_get_id(joblist, id, false);
}

Job* joblist_get_bg_job(Joblist* joblist)
{
    return joblist_get_job(joblist, false);
}

Job* joblist_get_fg_pid(Joblist* joblist, pid pid)
{
    return joblist_get_pid(joblist, pid, true);
}

Job* joblist_get_fg_id(Joblist* joblist, memmax id)
{
    return joblist_get_id(joblist, id, true);
}

Job* joblist_get_fg_job(Joblist* joblist)
{
    return joblist_get_job(joblist, true);
}

/* ------------------------------------------ */




/* ========= JOB ========= */

void Job_init(Job* job, byte connection, ubyte bg)
{
    job->pgid = 0;
    job->notified = 0;
    job->connection = connection;
    job->foreground = (!bg || (connection != JC_NONE));
    ArrayProcess_init(&job->processes);
    job->id = 0;
    job->tmodes = ashe.sh_term.tm_dfltermios;
}

void Job_free(Job* job)
{
    ProcessArray_free(&job->processes, Buffer_free)
}

void Joblist_free(Joblist* joblist)
{
    memmax len = joblist->jobs.len;;
    while(len--) {
        Job* job = Joblist_get_job(joblist, len);
        Job_kill_and_harvest(job);
    }
    JobArray_free(&joblist->jobs, Job_free);
}

Process* job_at(Job* job, memmax i)
{
    return vec_index(job->processes, i);
}

void job_format(Job* job, byte* fmt, ...)
{
    memmax  len = job_len(job);
    va_list argp;

    va_start(argp, fmt);

    ATOMIC_PRINT({
        fprintf(stderr, "\r\n" obrack bold(byellow("J_%ld")) cbrack obrack blue("\""), job->id);
        for(memmax i = 0; i < len; i++)
            fprintf(stderr, bred("%s %s"), job_at(job, i)->cmd, (i + 1 == len) ? "" : "| ");
        fprintf(stderr, blue("\b\"") cbrack cyan(" -> "));
        vfprintf(stderr, fmt, argp);
        fprintf(stderr, "\r\n");
    });

    va_end(argp);
}

ubyte job_add_process(Job* job, Process* process)
{
    if(__glibc_unlikely(!vec_push(job->processes, process))) {
        ATOMIC_PRINT(PW_PROCTOJOB(process->pid));
        return false;
    }
    return true;
}

Job* joblist_getjob(Joblist* jlist, pid pgid)
{
    vec_t* list = jlist->jobs;
    memmax len  = vec_len(list);
    Job* job;
    for(memmax i = 0; i < len; i++)
        if((job = vec_index(list, i))->pgid == pgid) return job;
    return NULL;
}

ubyte job_stopped(Job* job)
{
    memmax len = job_len(job);
    for(memmax i = 0; i < len; i++)
        if(job_at(job, i)->stopped) return true;
    return false;
}

ubyte job_completed(Job* job)
{
    memmax len = job_len(job);
    for(memmax i = 0; i < len; i++)
        if(!job_at(job, i)->completed) return false;
    return true;
}

Process* job_lastp(Job* job)
{
    return vec_back(job->processes);
}

static int32 job_wait(Job* job)
{
    int32   status;
    pid pid;

    do {
        pid = waitpid(-job->pgid, &status, WUNTRACED);
    } while(job_update(job, pid, status) && !job_stopped(job) && !job_completed(job));

    if(job_stopped(job)) return 0;
    else return job_lastp(job)->status;
}

static void Job_kill_and_harvest(Job* job)
{
    pid  pid;
    memmax zombies = 0;

    if(kill(-job->pgid, SIGKILL) < 0) {
        ATOMIC_PRINT({
            pwarn("failed sending a SIGKILL to process group ID:%d", job->pgid);
            perr();
        });
    }

    do {
        pid = waitpid(-job->pgid, NULL, WNOHANG);
        if(pid == 0) zombies++;
    } while(errno != ECHILD);

    if(__glibc_unlikely(zombies > 0))
        pwarn("%d zombie processes not reaped in the process group ID %d", zombies, job->pgid);
}

ubyte job_update(Job* job, pid pid, int32 status)
{
    memmax     len  = job_len(job);
    Process* proc = NULL;

    if(pid > 0) {
        for(memmax i = 0; i < len; i++) {
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
            proc->cmd);
        return false;
    } else if(ECHILD == errno) {
        return false;
    } else {
        perr();
        return false;
    }
}

int32 job_move_to_fg(Job* job, ubyte cont)
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
    int32 status = job_wait(job);

    /* If job was originally ran in the foreground without being stopped prior
     * and now it is stopped, then move it into the joblist */
    if(__glibc_unlikely(!cont && job_stopped(job) && !joblist_push(&ashe.sh_jlist, job))) {
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

void job_move_to_bg(Job* job, ubyte cont)
{
    job->foreground = false;
    if(cont)
        if(__glibc_unlikely(kill(-job->pgid, SIGCONT) < 0)) ATOMIC_PRINT(perr(););
}

memmax job_len(Job* job)
{
    return vec_len(job->processes);
}

void job_sa_running(Job* job)
{
    memmax len = job_len(job);
    for(memmax i = 0; i < len; i++) job_at(job, i)->stopped = false;
    job->notified = 0;
}

void job_continue(Job* job, ubyte foreground)
{
    job_sa_running(job);
    if(foreground) {
        job_move_to_fg(job, true);
        if(__glibc_unlikely(job_completed(job) && !joblist_remove_job(&ashe.sh_jlist, job))) {
            /// Invariant broken
            ATOMIC_PRINT(pwarn("FIX ME: tried removing background job that was not in the joblist!"));
            exit(EXIT_FAILURE);
        }
    } else job_move_to_bg(job, true);
}

void Process_init(Process* proc, pid pid, char* argv)
{
    proc->status = 0;
    proc->stopped = 0;
    proc->completed = 0;
    proc->pid = pid;
    proc->cmd = argv;
}

void Process_free(Process* proc)
{
    Buffer_free(&(proc)->cmd, NULL);
}

static void process_format(Process* p, byte* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);

    ATOMIC_PRINT({
        fprintf(
            stderr,
            "\r\n" obrack bold(blue("P_%d")) cbrack obrack blue("\"") bred("%s") blue("\"")
                cbrack                                     cyan(" -> "),
            p->pid,
            p->cmd);
        vfprintf(stderr, fmt, argp);
        fprintf(stderr, "\r\n");
    });

    va_end(argp);
}

static ubyte update_process(Joblist* joblist, pid pid, int32 status)
{
    memmax     joblistn = joblist_len(joblist);
    Process* proc     = NULL;

    if(pid > 0) {
        for(memmax i = 0; i < joblistn; i++) {
            Job* job  = Joblist_get_job(joblist, i);
            memmax jobn = job_len(job);

            for(memmax j = 0; j < jobn; j++) {
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
            proc->cmd);
        return false;
    } else if(pid == 0 || errno == ECHILD) {
        return false;
    } else {
        ATOMIC_PRINT({ perr(); });
        return false;
    }

    return false;
}
