#include "jobctl.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/wait.h>

joblist_t joblist = {0};

/// JOBLIST
size_t joblist_id();
/// JOB
size_t job_len(job_t *job);
bool job_stopped(job_t *job);
bool job_completed(job_t *job);
/// PROCESS
void process_drop(process_t *process);

bool joblist_init()
{
    joblist.jobs = vec_new(sizeof(job_t));
    if(__glibc_unlikely(is_null(joblist.jobs)))
        return false;
    joblist.next_id = 1;
    return true;
}

bool joblist_push(job_t *job)
{
    job->id = joblist_id();
    return vec_push(joblist.jobs, job);
}

size_t joblist_id()
{
    if(joblist_len() == 0)
        joblist.next_id = 1;
    return joblist.next_id++;
}

job_t *joblist_last(void)
{
    return vec_back(joblist.jobs);
}

job_t *joblist_at(size_t i)
{
    return vec_index(joblist.jobs, i);
}

void joblist_remove(size_t i)
{
    vec_remove(joblist.jobs, i, (FreeFn) job_drop);
}

job_t job_new(byte connection, bool bg)
{
    return (job_t){
        .pgid = 0,
        .notified = false,
        .connection = connection,
        .foreground = !bg || connection & (JC_AND | JC_OR),
        .processes = vec_new(sizeof(process_t)),
        .id = 0,
    };
}

void process_drop(process_t *process)
{
    free(process->commandline);
}

void joblist_drop(void)
{
    vec_drop(&joblist.jobs, (FreeFn) job_drop);
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
        fprintf(stderr, "\nJob [%ld]~[", job->id);
        for(size_t i = 0; i < len; i++)
            fprintf(
                stderr, "%s %s", job_at(job, i)->commandline, (i + 1 == len) ? "" : "| ");
        fprintf(stderr, "\b]: ");
        vfprintf(stderr, fmt, argp);
        fprintf(stderr, "\n");
    });

    va_end(argp);
}

bool job_add_process(job_t *job, process_t *process)
{
    if(__glibc_unlikely(!vec_push(job->processes, process))) {
        ATOMIC_PRINT({ pwarn("failed adding process [ID:%d] to job", process->pid); });
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
        if(!job_at(job, i)->completed) {
            fprintf(stderr, "process at %ld is not completed\n", i);
            return false;
        }
    return true;
}

void process_format(process_t *p, byte *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);

    ATOMIC_PRINT({
        fprintf(stderr, "Process [PID:%d] <%s>: ", p->pid, p->commandline);
        vfprintf(stderr, fmt, argp);
        fprintf(stderr, "\n");
    });

    va_end(argp);
}

bool update_process(pid_t pid, int status)
{
    size_t joblistn = joblist_len();
    if(pid > 0) {
        for(size_t i = 0; i < joblistn; i++) {
            job_t *job = joblist_at(i);
            size_t jobn = job_len(job);

            for(size_t j = 0; j < jobn; j++) {
                process_t *proc = job_at(job, j);

                if(proc->pid == pid) {
                    proc->status = status;
                    if(WIFSTOPPED(status)) {
                        proc->stopped = true;
                    } else {
                        proc->completed = true;
                        if(WIFSIGNALED(status)) {
                            ATOMIC_PRINT({
                                process_format(
                                    proc,
                                    "was TERMINATED by signal %d",
                                    proc->status = WTERMSIG(status));
                            });
                        } else if(WIFEXITED(status)) {
                            proc->status = WEXITSTATUS(status);
                        }
                    }
                    return true;
                }
            }
        }
        /// Idk how this can even happen, invariant broken (code bug)?
        ATOMIC_PRINT({ pwarn("no child process found [pid:%d]", pid); });
        return false;
    } else if(pid == 0 || errno == ECHILD) {
        ATOMIC_PRINT({ fprintf(stderr, "no more children to wait on\n"); });
        return false;
    } else {
        ATOMIC_PRINT({ perr(); });
        return false;
    }

    return false;
}

size_t joblist_len(void)
{
    return vec_len(joblist.jobs);
}

process_t *job_lastp(job_t *job)
{
    return vec_back(job->processes);
}

int job_wait(job_t *job)
{
    int status;
    pid_t pid;

    do {
        pid = waitpid(-job->pgid, &status, WUNTRACED);
    } while(update_process(pid, status) && !job_stopped(job) && !job_completed(job));

    return job_lastp(job)->status;
}

int job_move_to_fg(job_t *job, bool cont)
{
    /* put the job into the foreground */
    tcsetpgrp(TERMINAL_FD, job->pgid); /* Can't fail */

    if(cont) {
        if(__glibc_unlikely(
               tcsetattr(TERMINAL_FD, TCSADRAIN, &job->tmodes) < 0
               || kill(-job->pgid, SIGCONT) < 0))
        {
            ATOMIC_PRINT({ perr(); });
        }
    }

    int status = job_wait(job);

    if(!cont && job_stopped(job))
        if(__glibc_unlikely(!joblist_push(job))) {
            ATOMIC_PRINT({
                pwarn("failed adding job [PGID:%d] to the joblist", job->pgid);
                perr();
            });
            exit(EXIT_FAILURE);
        }

    /* put shell back into the foreground */
    tcsetpgrp(TERMINAL_FD, getpgrp()); /* Can't fail */

    if(__glibc_unlikely(
           tcgetattr(TERMINAL_FD, &job->tmodes)
           || tcsetattr(TERMINAL_FD, TCSADRAIN, &shell_tmodes) < 0))
    {
        ATOMIC_PRINT({
            pwarn("failed restoring shell's terminal modes");
            perr();
        });
    }

    return status;
}

void job_move_to_bg(job_t *job, bool cont)
{
    ATOMIC_PRINT({
        if(cont) {
            if(__glibc_unlikely(kill(-job->pgid, SIGCONT) < 0))
                perr();
            else
                job_format(job, "\x1B[33mstarted\x1B[0m");
        } else
            job_format(job, "\x1B[33mstarted\x1B[0m");
    });
}

size_t job_len(job_t *job)
{
    return vec_len(job->processes);
}

void joblist_update(void)
{
    int status;
    pid_t pid;

    do {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
        ATOMIC_PRINT({ printf("Updating process %d\n", pid); });
    } while(update_process(pid, status));
}

void joblist_update_and_notify(__attribute__((unused)) int signum)
{
    ATOMIC_PRINT({ printf("Running update of joblist\n"); });
    sigchld_recv = true;

    size_t len = joblist_len();
    ATOMIC_PRINT({ printf("len is %ld\n", len); });
    job_t *job;

    joblist_update();

    for(size_t i = 0; i < len;) {
        job = joblist_at(i);

        if(job_completed(job)) {
            printf("job completed is it foreground\n");
            if(!job->foreground) {
                printf("it is\n");
                ATOMIC_PRINT({
                    job_format(job, "\x1B[32mcompleted\x1B[0m");
                    pprompt();
                });
            }
            printf("it is not\n");
            joblist_remove(i);
            printf("removed the job from the joblist\n");
            len--;
            continue;
        } else if(job_stopped(job) && !job->notified) {
            printf("Job is stoppeeddd\n");
            ATOMIC_PRINT({
                job_format(job, "\x1B[31mstopped\x1B[0m");
                pprompt();
            });
            job->notified = true;
        }
        i++;
        printf("continuing to the next iteration\n");
    }
    printf("exited the joblist update function\n");
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
    if(foreground)
        job_move_to_fg(job, true);
    else
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
