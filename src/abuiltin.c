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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>

#include "abuiltin.h"
#include "autils.h"
#include "acommon.h"
#include "ajobcntl.h"
#include "ainput.h"
#include "ashell.h"

/* differentiate %ID (flip) and PID, check 'ashe_bi_jobs()' */
#define FLIP_SIGN_BIT(n) ((n) ^ ((a_uint32)1 << ((sizeof(n) * 8) - 1)))

/* options for envcmd function */
#define ENV_ADD	      0 /* add variable name/value */
#define ENV_SET	      1 /* add/overwrite variable name/value */
#define ENV_REMOVE    2 /* remove variable */
#define ENV_PRINT     3 /* print variable */
#define ENV_PRINT_ALL 4 /* print all variables */

/* help opt */
#define is_help_opt(arg) (strcmp((arg), "-h") == 0 || strcmp((arg), "--help") == 0)
#define print_help_opts(bin) \
	ashe_printf(stderr, "%s: use -h or --help options to display help information\r\n", bin);

/* builtin function signature */
typedef a_int32 (*builtinfn)(a_arr_ccharp *argv);

ASHE_PRIVATE inline void print_rows(const char **rows, a_uint32 len)
{
	a_uint32 i;

	for (i = 0; i < len; i++)
		ashe_printf(stderr, "%s\r\n", rows[i]);
}

ASHE_PRIVATE inline void ashe_jobs_print_header(void)
{
	const char *fmt = "\r\n%-*s%-*s%-*s\r\n";
	a_uint32 pad;

	pad = 10;
	ashe_printf(stderr, fmt, pad, "Job", pad, "Group", pad, "State");
}

ASHE_PRIVATE void ashe_jobs_print_job(struct a_job *job)
{
	const char *fmt = "%-*ld %-*d %-*s\r\n";
	const char *state;
	a_uint32 pad;
	a_ubyte completed, stopped;

	pad = 10;
	completed = a_job_is_completed(job);

	if (!completed) {
		stopped = a_job_is_stopped(job);
		state = (stopped ? "stopped" : "running");
	} else
		state = "completed";

	ashe_printf(stderr, fmt, pad, job->id, pad, job->pgid, pad, state);
}

ASHE_PRIVATE a_int32 filter_jobs_by_pid_or_id(a_arr_ccharp *argv, a_int32 *nums)
{
	const char *arg, *bin;
	char *errstr;
	a_ubyte id = 0;
	a_int32 num, status;
	a_memmax i;

	status = 0;
	bin = *a_arr_ccharp_index(argv, 0);

	for (i = 1; i < a_arrp_len(argv); i++) {
		arg = *a_arr_ccharp_index(argv, i);

		if ((id = (*arg == '%')))
			arg++;

		if (!isdigit(*arg)) {
			ashe_eprintf("%s: expected number, instead got '%s'.", bin, arg);
			print_help_opts(bin);
			a_defer(-1);
		}

		errno = 0;
		num = strtoul(arg, &errstr, 10);

		if (*errstr != '\0') {
			ashe_eprintf("%s: invalid %s provided", bin, (id ? "id" : "pid"));
		} else if (errno == ERANGE || num > INT_MAX) {
			ashe_eprintf("%s: %s '%s' too large, limit is %n.", bin,
				     (id ? "id" : "pid"), arg, INT_MAX);
		} else {
			*nums++ = (id ? (a_int32)FLIP_SIGN_BIT(num) : num);
			continue;
		}

		print_help_opts(bin);
		a_defer(-1);
	}
defer:
	return status;
}

ASHE_PRIVATE void ashe_jobs_print_filtered_jobs(a_int32 *nums, a_memmax len)
{
	struct a_job *job;
	a_memmax i;

	for (i = 0; i < len; i++) {
		if (nums[i] < 0)
			job = a_jobcntl_get_job_with_id(&ashe.sh_jobcntl, FLIP_SIGN_BIT(nums[i]));
		else
			job = a_jobcntl_get_job_with_pgid(&ashe.sh_jobcntl, nums[i]);
		if (job)
			ashe_jobs_print_job(job);
	}
}

ASHE_PRIVATE a_int32 ashe_bi_jobs(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"jobs - print currently running/stopped jobs.\r\n",
		"jobs [PID | %ID, [PID | %ID]]\r\n",
		"Prints all currently running jobs and their status.",
		"This command can accept arguments in the form of PID or ID (job id).",
		"If the PID is supplied then the output is restricted to jobs "
		"that contain the selected process ID.",
		"In case job ID is used, then the output will contain only the "
		"job with the matching job ID.",
	};

	struct a_jobcntl *jobcntl;
	struct a_job *job;
	a_memmax jobcnt, argc, i;
	a_int32 *nums, status;

	jobcntl = &ashe.sh_jobcntl;
	argc = a_arrp_len(argv);
	nums = NULL;
	status = 0;

	if (argc > 1)
		nums = ashe_calloc(sizeof(a_int32), argc - 1);

	switch (argc) {
	case 1:
		jobcnt = a_jobcntl_jobs(jobcntl);
		if (jobcnt == 0) {
			ashe_eprintf("jobs: there are no jobs running.");
			a_defer(-1);
		}
		ashe_jobs_print_header();
		for (i = 0; i < jobcnt; i++) {
			job = a_jobcntl_get_job_at(jobcntl, i);
			ashe_jobs_print_job(job);
		}
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
			break;
		}
		/* FALLTHRU */
	default:
		if (filter_jobs_by_pid_or_id(argv, nums) < 0)
			a_defer(-1);
		ashe_jobs_print_header();
		ashe_jobs_print_filtered_jobs(nums, argc - 1);
		break;
	}
defer:
	if (nums)
		ashe_free(nums);
	return status;
}

/* Auxiliary to ashe_bg() */
ASHE_PRIVATE a_int32 ashe_bg_last(void)
{
	struct a_job *job;

	job = a_jobcntl_get_job_from_background(&ashe.sh_jobcntl);

	if (job) {
		a_job_continue(job, 0);
		return 0;
	}
	ashe_eprintf("bg: there is no suitable job.");
	return -1;
}

/* Auxiliary to ashe_bg() */
ASHE_PRIVATE void ashe_bg_id(a_int32 id)
{
	struct a_job *job;

	job = a_jobcntl_get_job_with_id_from(&ashe.sh_jobcntl, id, 0);

	if (job) {
		a_job_continue(job, 0);
	} else
		ashe_eprintf("bg: could not find job with id %n.", id);
}

/* Auxiliary to ashe_bg() */
ASHE_PRIVATE void ashe_bg_pid(pid_t pid)
{
	struct a_job *job;

	job = a_jobcntl_get_job_with_pid_from(&ashe.sh_jobcntl, pid, 0);

	if (job)
		a_job_continue(job, 0);
	else
		ashe_eprintf("bg: could not find job with pid %n.", pid);
}

ASHE_PRIVATE void ashe_bg_background_filtered_jobs(a_int32 *nums, a_memmax len)
{
	a_memmax i;

	for (i = 0; i < len; i++) {
		if (nums[i] < 0)
			ashe_bg_id(FLIP_SIGN_BIT(nums[i]));
		else
			ashe_bg_pid(nums[i]);
	}
}

ASHE_PRIVATE a_int32 ashe_bi_bg(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"bg - sends jobs to background resuming them if they are paused.\n\r",
		"bg [PID | '%'ID [PID | '%'ID]]\n\r",
		"There are multiple ways of specifying which job to move/continue, either "
		"by PID, %ID or by not giving any arguments to the command.",
		"Multiple PIDs can be provided at once, same holds for %ID.",
		"If no additional arguments were provided then the last jobs that was "
		"created (the highest ID) that is in the background will be sent "
		"SIGCONT signal, signaling it to continue.",
	};

	a_memmax argc;
	a_int32 status;
	a_int32 *nums;

	status = 0;
	argc = a_arrp_len(argv);
	nums = 0;

	switch (argc) {
	case 1:
		status = ashe_bg_last();
		break;
	case 2:
		if (is_help_opt(*a_arr_ccharp_index(argv, 1))) {
			print_rows(usage, ASHE_ELEMENTS(usage));
			break;
		}
		/* FALLTHRU */
	default:
		nums = ashe_calloc(argc - 1, sizeof(a_int32));
		if (filter_jobs_by_pid_or_id(argv, nums) < 0)
			a_defer(-1);
		ashe_bg_background_filtered_jobs(nums, argc - 1);
		break;
	}
defer:
	if (nums)
		ashe_free(nums);
	return status;
}

/* Auxiliary to ashe_fg() */
ASHE_PRIVATE a_int32 ashe_fg_last(void)
{
	struct a_job *job;

	job = a_jobcntl_get_job_from_background(&ashe.sh_jobcntl);

	if (job) {
		a_job_continue(job, 1);
	} else {
		ashe_eprintf("fg: there is no suitable job.");
		return -1;
	}
	return 0;
}

/* Auxiliary to ashe_fg() */
ASHE_PRIVATE a_int32 ashe_fg_id(a_int32 id)
{
	struct a_job *job;

	job = a_jobcntl_get_job_with_id_from(&ashe.sh_jobcntl, id, 0);

	if (job) {
		a_job_continue(job, 1);
		return 0;
	}
	ashe_eprintf("fg: could not find job with id %n.", id);
	return -1;
}

/* Auxiliary to ashe_fg() */
ASHE_PRIVATE a_int32 ashe_fg_pid(pid_t pid)
{
	struct a_job *job;

	job = a_jobcntl_get_job_with_pid_from(&ashe.sh_jobcntl, pid, 0);

	if (job) {
		a_job_continue(job, 1);
		return 0;
	}
	ashe_eprintf("fg: could not find job with pid %n.", pid);
	return -1;
}

/* Foreground a job */
ASHE_PRIVATE a_int32 ashe_bi_fg(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"fg - move job to foreground\n\r",
		"fg [PID | %ID]\r\n",
		"Moves the job into the foreground additionally resuming it if it was stopped.",
		"If no arguments were supplied then the last job that was created (highest ID) "
		"that is still running or is stopped in the background will be used.",
		"User can provide PID or %ID to specify which job to foreground.",
	};

	a_memmax argc;
	a_int32 status, n;

	argc = a_arrp_len(argv);
	status = 0;
	n = 0;

	switch (argc) {
	case 1:
		status = ashe_fg_last();
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
		} else {
			if (filter_jobs_by_pid_or_id(argv, &n) < 0)
				a_defer(-1);
			if (n < 0)
				a_defer(ashe_fg_id(FLIP_SIGN_BIT(n)));
			a_defer(ashe_fg_pid(n));
		}
		break;
	default:
		print_help_opts("fg");
		a_defer(-1);
	}
defer:
	return status;
}

/* Exit shell */
ASHE_PRIVATE a_int32 ashe_bi_exit(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"exit - exits the shell\r\n",
		"exit [CODE]\r\n",
		"Exits the shell with the supplied CODE, if no code "
		"was provided, then the default 0 is used.",
	};
	static const char *exit_warn[] = {
		"There are background jobs that are still running or are stopped!",
		"Running exit again will result in possible termination of these jobs.",
	};

	a_memmax jobcnt, argc;
	char *endptr;
	a_int32 status;

	status = 0;
	argc = a_arrp_len(argv);

	switch (argc) {
	case 1:
		jobcnt = a_jobcntl_jobs(&ashe.sh_jobcntl);
		ashe.sh_flags.exit |= (ashe.sh_flags.isfork || jobcnt == 0);
		if (!ashe.sh_flags.exit) {
			ashe.sh_flags.exit = 1;
			print_rows(exit_warn, ASHE_ELEMENTS(exit_warn));
		} else {
			goto l_exit;
		}
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
			break;
		}
		status = strtol(a_arrp_ptr(argv)[1], &endptr, 10);
		if (errno == EINVAL || errno == ERANGE || *endptr != '\0' || status < 0) {
			ashe_eprintf("exit: invalid exit status CODE '%s'.", a_arrp_ptr(argv)[1]);
			a_defer(-1);
		} else {
l_exit:
			ashe_exit(status);
		}
		break;
	default:
		print_help_opts("exit");
		ashe.sh_flags.exit = 0;
		a_defer(-1);
	}
defer:
	return status;
}

/* Change working directory */
ASHE_PRIVATE a_int32 ashe_bi_cd(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"cd - change directory\r\n",
		"cd [DIRNAME]\r\n",
		"Changes current working directory.",
		"If no DIRNAME is given, then the $HOME environment "
		"variable is used as DIRNAME.",
	};

	a_int32 status;
	a_memmax argc;

	status = 0;
	argc = a_arrp_len(argv);

	switch (argc) {
	case 1:
		if (a_unlikely(chdir(getenv(HOME)) < 0)) {
			ashe_perrno("cd");
			a_defer(-1);
		}
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
		} else if (chdir(a_arrp_ptr(argv)[1]) < 0) {
			ashe_perrno("chdir");
			a_defer(-1);
		}
		break;
	default:
		print_help_opts("cd");
		a_defer(-1);
	}
defer:
	return status;
}

/* Auxiliary to envcmd() */
ASHE_PRIVATE inline void print_environ(void)
{
	a_int32 i = 0;

	while (__environ[i] != NULL)
		ashe_printf(stdout, "%s\r\n", __environ[i++]);
}

// clang-format off
/* Auxiliary function for handling environment variables */
ASHE_PRIVATE a_int32 envcmd(a_arr_ccharp *argv, a_int32 option)
{
	const char *temp;
	a_int32 status;

	status = 0;

	switch (option) {
	case ENV_ADD:
	case ENV_SET:
		if (setenv(a_arrp_ptr(argv)[1], a_arrp_ptr(argv)[2], option != ENV_ADD) < 0) {
			ashe_perrno("senv");
			a_defer(-1);
		}
		break;
	case ENV_REMOVE:
		if (a_unlikely(unsetenv(a_arrp_ptr(argv)[1]) < 0)) {
			ashe_perrno("renv");
			a_defer(-1);
		}
		break;
	case ENV_PRINT:
		if ((temp = getenv(a_arrp_ptr(argv)[1])) != NULL) {
			ashe_printf(stdout, "%s\r\n", temp);
			break;
		} else {
			ashe_eprintf("penv: variable '%s' doesn't exist.", a_arrp_ptr(argv)[1]);
			a_defer(-1);
		}
	case ENV_PRINT_ALL:
		print_environ();
		break;
	default: /* UNREACHED */
		ashe_assert(0);
		break;
	}
defer:
	return status;
}
// clang-format on

ASHE_PRIVATE a_int32 ashe_bi_penv(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"penv - print environment variable\r\n",
		"penv [NAME]\r\n",
		"Prints environment variable NAME; in case user didn't "
		"specify NAME, then all of the variables are printed.",
	};

	a_memmax argc;
	a_int32 status;

	status = 0;
	argc = a_arrp_len(argv);

	switch (argc) {
	case 1:
		status = envcmd(NULL, ENV_PRINT_ALL);
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1]))
			print_rows(usage, ASHE_ELEMENTS(usage));
		else
			status = envcmd(argv, ENV_PRINT);
		break;
	default:
		print_help_opts("penv");
		status = -1;
		break;
	}
	return status;
}

ASHE_PRIVATE a_int32 ashe_bi_senv(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"senv - set environment variable\r\n",
		"senv NAME VALUE\r\n",
		"Sets or adds variable in the environment.",
		"If variable with NAME already exists then its VALUE is "
		"overwritten.",
		"In case variable with NAME is not found, then it is newly "
		"created and added into the environment with the value set "
		"to match VALUE.",
		"If the NAME was provided but not the VALUE then the value "
		"of that variable will be an emtpy string.",
	};

	a_memmax argc;
	a_int32 status;

	status = 0;
	argc = a_arrp_len(argv);

	switch (argc) {
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
			break;
		}
		/* FALLTHRU */
	case 3:
		status = envcmd(argv, ENV_SET);
		break;
	default:
		print_help_opts("senv");
		status = -1;
		break;
	}
	return status;
}

ASHE_PRIVATE a_int32 ashe_bi_renv(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"renv - remove environment variable\r\n",
		"renv NAME\r\n",
		"Removes variable NAME from the environment.",
	};

	a_memmax argc;
	a_int32 status;

	status = 0;
	argc = a_arrp_len(argv);

	switch (argc) {
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1]))
			print_rows(usage, ASHE_ELEMENTS(usage));
		else
			status = envcmd(argv, ENV_REMOVE);
		break;
	default:
		print_help_opts("renv");
		status = -1;
	}
	return status;
}

ASHE_PRIVATE a_int32 ashe_bi_pwd(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"pwd - print current working directory\r\n",
		"pwd\r\n",
		"Prints the current working directory.",
	};

	char buff[PATH_MAX];
	a_memmax argc;
	a_int32 status;

	status = 0;
	argc = a_arrp_len(argv);

	switch (argc) {
	case 1:
		if (a_unlikely(!getcwd(buff, PATH_MAX))) {
			ashe_perrno("getcwd");
			a_defer(-1);
		}
		ashe_printf(stdout, "%s\r\n", buff);
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
			break;
		}
		/* FALLTHRU */
	default:
		print_help_opts("pwd");
		a_defer(-1);
	}
defer:
	return status;
}

ASHE_PRIVATE a_int32 ashe_bi_clear(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"clear - clears the screen\r\n",
		"clear\r\n",
		"Clears the terminal screen including scrollback.",
	};

	a_memmax argc;
	a_int32 status;

	status = 0;
	argc = a_arrp_len(argv);

	switch (argc) {
	case 1:
		ashe_clear_screen_unsafe();
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
			break;
		}
		/* FALLTHRU */
	default:
		print_help_opts("clear");
		status = -1;
		break;
	}
	return status;
}

ASHE_PRIVATE void print_builtins(void)
{
	static const char *builtin[] = {
		"cd",	"pwd",	"clear", "builtin", "fg",   "bg",
		"jobs", "exec", "exit",	 "penv",    "senv", "renv",
	};
	a_memmax i;

	for (i = 0; i < ASHE_ELEMENTS(builtin); i++)
		ashe_printf(stdout, "%s\r\n", builtin[i]);
}

ASHE_PRIVATE a_int32 ashe_bi_builtin(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"builtin - lists all builtin commands\r\n",
		"builtin\r\n",
		"Prints all of the builtin commands.",
	};

	a_memmax argc;

	argc = a_arrp_len(argv);

	switch (argc) {
	case 1:
		print_builtins();
		break;
	case 2:
		if (is_help_opt(a_arrp_ptr(argv)[1])) {
			print_rows(usage, ASHE_ELEMENTS(usage));
			break;
		}
		/* FALLTHRU */
	default:
		print_help_opts("builtin");
		return -1;
	}
	return 0;
}

ASHE_PRIVATE a_int32 ashe_bi_exec(a_arr_ccharp *argv)
{
	static const char *usage[] = {
		"exec - execute a command and open, close or copy file descriptors\r\n",
		"exec [command [argument...]]\r\n",
		"Exec will open, close and/or copy file descriptors as specified",
		"by any redirection as part of the command.\r\n",
		"If exec is specified with 'command' then it will replace the shell "
		"with that 'command' without creating a new process.",
		"Additionally if any arguments were specified they will be arguments "
		"to the 'command'.",
		"Redirections will persist in a new process image (if exec was invoked "
		"with a 'command')",
	};

	const char **execargs;
	a_memmax argc;
	a_uint32 i;

	argc = a_arrp_len(argv);

	if (argc == 1)
		return 0;
	if (is_help_opt(*a_arr_ccharp_index(argv, 1))) {
		print_rows(usage, ASHE_ELEMENTS(usage));
		return 0;
	}

	execargs = ashe_malloc(sizeof(*execargs) * argc);
	for (i = 0; i < argc - 1; i++)
		execargs[i] = *a_arr_ccharp_index(argv, i + 1);
	execargs[argc - 1] = NULL;

	if (a_unlikely(execvp(execargs[0], (char *const *)execargs) < 0)) {
		ashe_free(execargs);
		ashe_perrno("execvp");
		return -1;
	}
	/* UNREACHED */
	ashe_assert(0);
	return 0;
}

ASHE_PRIVATE inline a_int32 builtin_match(const char *str, a_uint32 start, a_uint32 len,
					  const char *pattern, enum a_builtin_type type)
{
	if (strlen(str) == start + len && memcmp(str + start, pattern, len) == 0)
		return type;
	return -1;
}

/* Check if the 'command' is builtin command and
 * return the 'builtin' enum value of it or -1. */
ASHE_PUBLIC a_int32 ashe_isbin(const char *command)
{
	a_int32 bi = -1;

	ashe_assert(command != NULL);

	switch (command[0]) {
	case 'b':
		switch (command[1]) {
		case 'u':
			return builtin_match(command, 2, 5, "iltin", TBI_BUILTIN);
		case 'g':
			if (command[2] == '\0')
				return TBI_BG;
			else
				return -1;
		default:
			break;
		}
		break;
	case 'c':
		switch (command[1]) {
		case 'd':
			if (command[2] == '\0')
				return TBI_CD;
			else
				return -1;
		case 'l':
			return builtin_match(command, 2, 3, "ear", TBI_CLEAR);
		default:
			break;
		}
		break;
	case 'e':
		switch (command[1]) {
		case 'x':
			switch (command[2]) {
			case 'e':
				return builtin_match(command, 2, 2, "ec", TBI_EXEC);
			case 'i':
				return builtin_match(command, 2, 2, "it", TBI_EXIT);
			default:
				break;
			}
		default:
			break;
		}
		break;
	case 'f':
		return builtin_match(command, 1, 1, "g", TBI_FG);
	case 'j':
		return builtin_match(command, 1, 3, "obs", TBI_JOBS);
	case 'p':
		switch (command[1]) {
		case 'e':
			return builtin_match(command, 2, 2, "nv", TBI_PENV);
		case 'w':
			return builtin_match(command, 2, 1, "d", TBI_PWD);
		default:
			break;
		}
		break;
	case 'r':
		return builtin_match(command, 1, 3, "env", TBI_RENV);
	case 's':
		return builtin_match(command, 1, 3, "env", TBI_SENV);
	default:
		break;
	}

	return bi;
}

/* Runs builting function 'bi'. */
ASHE_PUBLIC a_int32 ashe_runbin(struct a_simple_cmd *scmd, enum a_builtin_type tbi)
{
	static const builtinfn table[] = {
		ashe_bi_builtin, ashe_bi_bg,   ashe_bi_cd,   ashe_bi_clear,
		ashe_bi_fg,	 ashe_bi_jobs, ashe_bi_penv, ashe_bi_pwd,
		ashe_bi_renv,	 ashe_bi_senv, ashe_bi_exec, NULL /* ashe_bi_exit */,
	};

	ashe_assertf(tbi >= TBI_BUILTIN && tbi <= TBI_EXIT, "invalid tbi");
	if (a_unlikely(tbi == TBI_EXIT))
		return ashe_bi_exit(&scmd->sc_argv);
	ashe.sh_flags.exit = 0;
	return table[tbi](&scmd->sc_argv);
}
