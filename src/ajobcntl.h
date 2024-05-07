/* ----------------------------------------------------------------------------------------------
 * Copyright (C) 2023-2024 Jure Bagić
 *
 * This file is part of ashe.
 * ashe is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ashe is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ashe.
 * If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------------------------*/

#ifndef AJOBCNTL_H
#define AJOBCNTL_H

#include "acommon.h"
#include "aarray.h"
#include "atoken.h"

#include <termios.h>

struct a_process {
	a_pid pid; /* process ID */
	a_int32 status; /* exit status */
	a_ubyte stopped : 1; /* flag indicating if process is stopped */
	a_ubyte completed : 1; /* flag indicating if process is finished executing */
};

ARRAY_NEW(a_arr_process, struct a_process)

void a_process_init(struct a_process *proc, a_pid pid);

struct a_job {
	struct termios tmodes; /* Terminal attributes/settings */
	a_arr_process processes; /* processes belonging to the job */
	a_memmax id; /* job id, ordered (1,2,3...) */
	a_pid pgid; /* process group ID */
	const char *input; /* block (debug) */
	a_ubyte notified : 1; /* set if job state changed and it notified the shell */
	a_ubyte foreground : 1; /* set if job is running in foreground */
};

ARRAY_NEW(a_arr_job, struct a_job)

void a_job_init(struct a_job *job, const char *dbginput, a_ubyte isbg);
a_memmax a_job_processes(struct a_job *job);
void a_job_add_process(struct a_job *job, struct a_process process);
a_ubyte a_job_is_stopped(struct a_job *job);
a_ubyte a_job_is_completed(struct a_job *job);
void a_job_mark_as_background(struct a_job *job, a_ubyte cont);
a_int32 a_job_move_to_foreground(struct a_job *job, a_ubyte cont, a_ubyte *stop);
void a_job_continue(struct a_job *job, a_ubyte isfg);
void a_job_free(struct a_job *job);

/* ===== Interface to job control ==== */

struct a_jobcntl { /* wrapper job control interface */
	a_arr_job jobs; /* running/stopped jobs */
};

void a_jobcntl_init(struct a_jobcntl *jobcntl);

a_memmax a_jobcntl_jobs(struct a_jobcntl *jobcntl);
struct a_job *a_jobcntl_get_job_at(struct a_jobcntl *jobcntl, a_uint32 i);
void a_jobcntl_add_job(struct a_jobcntl *jobcntl, struct a_job *job);
a_ubyte a_jobcntl_remove_job(struct a_jobcntl *jobcntl, struct a_job *job,
			     struct a_job *out);
void a_jobcntl_update_and_notify(struct a_jobcntl *jobcntl);

struct a_job *a_jobcntl_get_job_with_id(struct a_jobcntl *jobcntl, a_memmax id);
struct a_job *a_jobcntl_get_job_with_pid(struct a_jobcntl *jobcntl, a_pid pid);
struct a_job *a_jobcntl_get_job_with_pgid(struct a_jobcntl *jobcntl, a_pid gpid);

struct a_job *a_jobcntl_get_job_from(struct a_jobcntl *jobcntl, a_ubyte where);
#define a_jobcntl_get_job_from_foreground(jobcntl) a_jobcntl_get_job_from(jobcntl, 1)
#define a_jobcntl_get_job_from_background(jobcntl) a_jobcntl_get_job_from(jobcntl, 0)
struct a_job *a_jobcntl_get_job_with_id_from(struct a_jobcntl *jobcntl, a_memmax id,
					     a_ubyte where);
struct a_job *a_jobcntl_get_job_with_pid_from(struct a_jobcntl *jobcntl, a_pid pid,
					      a_ubyte where);
struct a_job *a_jobcntl_get_job_with_pgid_from(struct a_jobcntl *jobcntl, a_pid pgid,
					       a_ubyte where);

struct a_job *a_jobcntl_get_job_with_id(struct a_jobcntl *jobcntl, a_memmax id);
struct a_job *a_jobcntl_get_job_with_pid(struct a_jobcntl *jobcntl, a_pid pid);
struct a_job *a_jobcntl_get_job_with_pgid(struct a_jobcntl *jobcntl, a_pid pgid);
struct a_job *a_jobcntl_get_foreground_job(struct a_jobcntl *jobcntl);

void a_jobcntl_harvest(struct a_jobcntl *jobcntl);
void a_jobcntl_free(struct a_jobcntl *jobcntl);

#endif
