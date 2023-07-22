#include "job.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

job_t job_new(byte connection, bool bg)
{
    return (job_t){
        .pgid = 0,
        .notified = false,
        .connection = connection,
        .foreground = !bg || connection & (JC_AND | JC_OR),
        .processes = vec_new(sizeof(process_t)),
    };
}

void process_drop(process_t *process)
{
    free(process->commandline);
}

void joblist_cleanup(void)
{
    vec_drop(&joblist.jobs, (FreeFn) job_drop);
}

void job_drop(job_t *job)
{
    if(kill(-job->pgid, SIGKILL) < 0) {
        pwarn("failed to kill processes in pgid:%d", job->pgid);
        perr();
    }

    vec_drop(&job->processes, (FreeFn) process_drop);
}

void job_print_launch(job_t *job, size_t index)
{
    printf("[%ld]: %d", index, job->pgid);
}

bool job_add_process(job_t *job, process_t *process)
{
    if(!vec_push(job->processes, process)) {
        pwarn("failed adding process [ID:%d] to job", process->pid);
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
    vec_t *procs = job->processes;
    size_t len = vec_len(procs);
    process_t *p = NULL;
    for(size_t i = 0; i < len; i++)
        if(!(p = vec_index(procs, i))->completed && !p->stopped)
            return false;
    return true;
}

bool job_completed(job_t *job)
{
    vec_t *procs = job->processes;
    size_t len = vec_len(procs);
    process_t *p = NULL;
    for(size_t i = 0; i < len; i++)
        if(!(p = vec_index(procs, i))->completed)
            return false;
    return true;
}

bool update_process(pid_t pid, int status)
{
    size_t joblistn = joblist_len();
    if(pid > 0) {
        for(size_t i = 0; i < joblistn; i++) {
            job_t *job = vec_index(joblist.jobs, i);
            size_t jobn = job_procn(job);

            for(size_t j = 0; j < jobn; j++) {
                process_t *proc = vec_index(job->processes, j);

                if(proc->pid == pid) {
                    proc->status = status;
                    if(WIFSTOPPED(status)) {
                        /// After waitpid returned positive pid then child is either
                        /// stopped
                        proc->stopped = true;
                    } else {
                        /// or completed
                        proc->completed = true;
                        if(WIFSIGNALED(status)) {
                            pwarn(
                                "process [pid:%d] was terminated by signal %d",
                                pid,
                                WTERMSIG(status));
                        }
                    }
                    return true;
                }
            }
        }
        /// Idk how this can even happen, invariant broken (code bug)?
        pwarn("no child process found [pid:%d]", pid);
        return false;
    } else if(pid == 0 || pid == ECHILD) {
        /// Child either exist but its state is not changed (stopped),
        /// or this process has no children to wait on (they are all reaped)
        return false;
    } else {
        /// waitpid error occured print error and return
        perr();
        return false;
    }

    return false;
}

size_t joblist_len(void)
{
    return vec_len(joblist.jobs);
}

void job_wait(job_t *job)
{
    int status;
    pid_t pid;

    /// Loop until all the processes in the 'job' are either stopped or completed
    do {
        pid = waitpid(-job->pgid, &status, WUNTRACED);
    } while(update_process(pid, status));
}

void job_move_to_fg(job_t *job, bool cont)
{
    if(tcsetpgrp(TERMINAL_FD, job->pgid) < 0) {
        pwarn("failed moving job [pgid:%d] to foreground", job->pgid);
        perr();
    }

    if(cont) {
        if(tcsetattr(TERMINAL_FD, TCSADRAIN, &job->tmodes) < 0
           || kill(job->pgid, SIGCONT) < 0)
        {
            perr();
        }
    }

    job_wait(job);

    pid_t spgid = getpgrp();

    if(tcsetpgrp(TERMINAL_FD, spgid) < 0) {
        pwarn("failed moving shell [pgid:%d] to foreground", spgid);
        perr();
    }

    if(tcgetattr(TERMINAL_FD, &job->tmodes)
       || tcsetattr(TERMINAL_FD, TCSADRAIN, &shell_tmodes) < 0)
    {
        pwarn("failed restoring shell's terminal modes");
        perr();
    }
}

void job_move_to_bg(job_t *job, bool cont)
{
    if(cont) {
        if(kill(job->pgid, SIGCONT) < 0)
            perr();
    }
}

size_t job_procn(job_t *job)
{
    return vec_len(job->processes);
}

process_t process_new(pid_t pid)
{
    return (process_t){
        .status = 0,
        .stopped = false,
        .completed = false,
        .pid = pid,
        .commandline = NULL,
    };
}
