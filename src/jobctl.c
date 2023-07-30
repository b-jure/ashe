#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/wait.h>

#define POLITE(status) (WTERMSIG((status)) & (SIGTERM | SIGINT | SIGQUIT | SIGKILL | SIGHUP))

#define job_completed_format green("completed") obrack red("-") cbrack
#define job_stopped_format   red("stopped") obrack magenta("!") cbrack
#define job_started_format   byellow("started") obrack green("+") cbrack
#define print_sigterm(status, polite)                                                              \
    process_format(                                                                                \
        proc,                                                                                      \
        "was" red("TERMINATED") "by signal" blue("%d") "%s",                                       \
        status,                                                                                    \
        ((polite) ? " (" italic("polite") ")" : ""))

joblist_t joblist = {0};
static size_t joblist_id();

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

static size_t joblist_id()
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
        .tmodes = dflterm,
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

bool joblist_pop_job(job_t *job, job_t *out)
{
    size_t len = joblist_len();
    for(size_t i = 0; i < len; i++) {
        if(joblist_at(i)->id == job->id) {
            vec_pop_at(joblist.jobs, out, i);
            return true;
        }
    }
    return false;
}

void process_format(process_t *p, byte *fmt, ...)
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

bool update_process(pid_t pid, int status)
{
    size_t joblistn = joblist_len();
    process_t *proc = NULL;

    if(pid > 0) {
        for(size_t i = 0; i < joblistn; i++) {
            job_t *job = joblist_at(i);
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
    } while(job_update(job, pid, status) && !job_stopped(job) && !job_completed(job));

    if(job_stopped(job))
        return 0;
    else
        return job_lastp(job)->status;
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
    tcsetpgrp(TERMINAL_FD, job->pgid); /* Can't fail */

    if(cont) {
        if(__glibc_unlikely(
               tcsetattr(TERMINAL_FD, TCSADRAIN, &job->tmodes) < 0
               || kill(-job->pgid, SIGCONT) < 0))
        {
            ATOMIC_PRINT(perr());
        }
    }

    int status = job_wait(job);

    if(__glibc_unlikely(!cont && job_stopped(job) && !joblist_push(job))) {
        ATOMIC_PRINT(PW_ADDJ(job));
        exit(EXIT_FAILURE);
    }

    /* put shell back into the foreground */
    tcsetpgrp(TERMINAL_FD, getpgrp()); /* Can't fail */

    if(__glibc_unlikely(
           tcgetattr(TERMINAL_FD, &job->tmodes) || tcsetattr(TERMINAL_FD, TCSADRAIN, &dflterm) < 0))
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
    if(cont)
        if(__glibc_unlikely(kill(-job->pgid, SIGCONT) < 0))
            ATOMIC_PRINT(perr(););
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
    } while(update_process(pid, status));
}

void joblist_update_and_notify(__attribute__((unused)) int signum)
{
    size_t len = joblist_len();
    job_t *job;

    joblist_update();

    for(size_t i = 0; i < len;) {
        job = joblist_at(i);

        if(job_completed(job)) {
            /// pprompt format depends on joblist len this is why
            /// we explicitly remove job in both branches ordering
            /// is important to print correct job len in the prompt
            if(!job->foreground) {
                ATOMIC_PRINT({
                    job_format(job, job_completed_format);
                    joblist_remove(i);
                    pprompt();
                    if(reading)
                        inbuff_print(&terminal_input, true);
                });
            } else {
                joblist_remove(i);
            }

            len--;
            continue;
        } else if(job_stopped(job) && !job->notified) {
            ATOMIC_PRINT({
                job_format(job, job_stopped_format);
                pprompt();
                if(reading)
                    inbuff_print(&terminal_input, true);
            });
            job->notified = true;
        }
        i++;
    }
}

void job_sa_running(job_t *job)
{
    size_t len = job_len(job);
    for(size_t i = 0; i < len; i++)
        job_at(job, i)->stopped = false;
    job->notified = 0;
}

bool joblist_remove_job(job_t *job)
{
    size_t len = joblist_len();
    for(size_t i = 0; i < len; i++) {
        if(joblist_at(i)->id == job->id) {
            joblist_remove(i);
            return true;
        }
    }
    return false;
}

void job_continue(job_t *job, bool foreground)
{
    job_sa_running(job);
    if(foreground) {
        job_move_to_fg(job, true);
        if(__glibc_unlikely(job_completed(job) && !joblist_remove_job(job))) {
            /// Invariant broken
            ATOMIC_PRINT(
                pwarn("FIX ME: tried removing background job that was not in the joblist!"));
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
