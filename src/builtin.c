#include "builtin.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"

int fg_id(int id)
{
    size_t len = joblist_len();
    job_t *job;

    for(size_t i = 0; i < len; i++) {
        job = joblist_at(i);
        if(job->id == (size_t) id) {
            job_continue(job, true);
            return SUCCESS;
        }
    }

    PW_FGID_ERR(id);
    return FAILURE;
}

int fg_pgid(pid_t pgid)
{
    size_t len = joblist_len();
    job_t *job;

    for(size_t i = 0; i < len; i++) {
        job = joblist_at(i);
        if(job->pgid == pgid) {
            job_continue(job, true);
            return SUCCESS;
        }
    }

    PW_FGPGID_ERR(pgid);
    return FAILURE;
}

int fg_last(void)
{
    job_t *job;

    job = joblist_last();
    if(is_some(job)) {
        job_continue(job, true);
        return SUCCESS;
    }

    PW_FG_ERR;
    return FAILURE;
}

int fg(pid_t pgid, int id)
{
    if(pgid >= 0) {
        return fg_pgid(pgid);
    } else if(id >= 0) {
        return fg_id(id);
    } else {
        return fg_last();
    }
}
