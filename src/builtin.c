#include "async.h"
#include "builtin.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"

#include <errno.h>
#include <string.h>

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

// clang-format off
int exit_builtin(byte *const *argv, bool shell)
{
    bool status = FAILURE;
    static const byte* usage = "exit [CODE]";
    static const byte *exit_warn
        = "There are still background jobs running!\n\nRun "
          bold(green("exit"))" again to exit, this will result in "
          bold(bred("termination")) " of child processes that are still running or are stopped.";

    if(is_null(argv[1])) {
        if(shell) {
            if(joblist_len() != 0 && !exit_warning) {
                exit_warning = true;
                ATOMIC_PRINT(pwarn("%s", exit_warn));
            } else
                exit(SUCCESS);
        }
        status = SUCCESS;
    } else if(is_some(argv[1]) && is_null(argv[2])) {
        int exit_status;
        if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            ATOMIC_PRINT({
                fprintf(stderr, bold(green("exit")) " - exits the shell");
                pusage("%s", usage);
                fprintf(
                    stderr,
                    bold(green("exit")) " is a builtin command that exits the "
                    "shell with the " italic("CODE") " if supplied or 0 in case " italic("CODE")
                    " is not supplied. "
                    "In case there are background jobs running when exit is called, "
                    "shell will not exit instantly, instead it will warn user first "
                    "if there are any background jobs still running.");
            });
        } else {
            exit_status = strtol(argv[1], NULL, 10);
            if(errno == EINVAL || errno == ERANGE) {
                ATOMIC_PRINT({
                    pwarn("invalid exit status integer");
                    pusage("%s", usage);
                    fprintf(
                        stderr,
                        "\nThe -h or --help options display help information this command\n");
                });
            } else {
                status = exit_status;
                if(shell) {
                    if(joblist_len() != 0 && !exit_warning) {
                        exit_warning = true;
                        ATOMIC_PRINT(pwarn("%s", exit_warn));
                    } else
                        exit(exit_status);
                }
            }
        }
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pusage("%s", usage);
        });
        status = FAILURE;
        exit_warning = false;
    }

    return status;
}
