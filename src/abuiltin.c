#include "acommon.h"
#include "errors.h"
#include "jobctl.h"
#include "input.h"
#include "shell.h"
#include "async.h"
#include "parser.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>



#define BUILTIN_N (sizeof(builtin)/sizeof(builtin[0]))
static const char* builtin[] = {
    "exit",
    "pwd",
    "clear",
    "cd",
    "penv",
    "senv",
    "renv",
    "builtin",
    "fg",
    "bg",
    "jobs",
};



/* options for envcmd function */
#define ENV_ADD         0 /* add variable name/value */
#define ENV_SET         1 /* add/overwrite variable name/value */
#define ENV_REMOVE      2 /* remove variable */
#define ENV_PRINT       3 /* print variable */
#define ENV_PRINT_ALL   4 /* print all variables */



#define is_help_opt(arg) (strcmp((arg), "-h") == 0 || strcmp((arg), "--help") == 0)
#define HELPSTR "\nThe -h or --help options display help information for this command\n"
#define print_help_opts() fprintf(stderr, HELPSTR)



#define namef(name)       bold(green(#name))
#define keywordf(keyword) bold(cyan(#keyword))
#define validf(valid)     bold(blue(valid))
#define invalidf(invalid) bold(bred(invalid))



/// This is used in 'bg' builtin command when parsing and storing combination
/// of id and pid integers in the same array to later distinguish them by their sign bit.
/// All valid PID's and ID's are positive so by flipping a sign bit we can differentiate
/// them while also storing them in the same array without storing each inside a tagged
/// union.
#define FLIP_SIGN_BIT(integer)                                                           \
    ((int32)(((int32)(integer)) ^ (~((uint32)0) << ((sizeof(uint32) * 8) - 1))))



static void builtin_printf(const char* builtin, const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    fprintf(stderr, "[%s]: ", builtin);
    vfprintf(stderr, fmt, argp);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(argp);
}


typedef enum {
    OPTION_NAME,
    OPTION_DESC,
    OPTION_USAGE,
} Option;

static void print_help(Option type, const char* str)
{
    static const char* option[] = {
        "NAME",
        "DESCRIPTION",
        "SYNOPSIS",
    };

    block_signals();
    const char* opt = option[type];
    memmax len = strlen(str);
    char buffer[len + 1];
    memcpy(buffer, str, len + 1);

    uint32 col_limit = ashe.sh_term.tm_columns - 10;
    memmax cap = len + (len / col_limit) + 2;
    char buff[cap];
    char* target = buff;
    buff[0] = '\0';
    memset(buff, 0, cap);

    uint32 remaining_space = col_limit;
    const char* delimiter = " ";
    const char* word = strtok(buffer, delimiter);
    char* ptr = NULL;

    while(word) {
        memmax word_len = len_without_seq(word);
        if(remaining_space > word_len) {
            target += sprintf(target, "%s ", word);
            remaining_space -= (word_len + 1); // Account for delimiter (1)
        } else {
            target += sprintf(target, "\n%s ", word);
            remaining_space = col_limit - word_len - 1; // Account for delimiter (1)
        }
        if((ptr = strchr(word, '\n')) != NULL)
            remaining_space = col_limit - strlen(ptr + 1);
        word = strtok(NULL, delimiter);
    }

    fprintf(stderr, "\n" yellow(bold("%s")) "\n%-s\n", opt, buff);
    fflush(stderr);
    unblock_signals();
}

static finline void print_manpage(const char* name, const char* usage, const char* desc)
{
    print_help(OPTION_NAME, name);
    print_help(OPTION_USAGE, usage);
    print_help(OPTION_DESC, desc);
}





/* Header format */
#define HFMT "%-*s"
static finline void ashe_jobs_print_header(void) 
{
    const char* fmt = "\n" HFMT HFMT HFMT "\n";
    uint32 padding = 10;
    printf(fmt, padding, "Job", padding, "Group", padding, "State");
}

static void ashe_jobs_print_job(Job* job)
{
    ubyte completed, stopped;
    uint32 padding = 10;
    const char* state = NULL;
    completed = Job_iscompleted(job);
    if(!completed) {
        stopped = Job_isstopped(job); 
        state = (stopped ? red("stopped") : yellow("running"));
    } else state = green("completed");
    printf("%-*ld %-*d %-*s\n", 10, job->id, 10, job->pgid, 10, state);
}

static int32 get_integers(ArrayBuffer* argv, int32 *out, memmax len)
{
    char *intstr;
    char *errstr;
    ubyte id = 0;
    int32 integer;
    memmax precision;

    while(len--) {
        intstr = *argv++;
        if(*intstr == '%') {
            id = 1;
            if(*++intstr == '\0') {
                builtin_printf("jobs", "provided '%' but missing ID");
                print_help_opts();
                return -1;
            }
        }

        integer = strtol(intstr, &errstr, 10);

        if(*errstr != '\0') {
            precision = errstr - intstr;
            if(precision == 0)
                intstr = "";
            ATOMIC_PRINT({
                pwarn(
                    "invalid integer provided >> " validf("%*s") invalidf("%s") " <<",
                    precision,
                    intstr,
                    errstr
                );
            });
            return -1;
        } else if(errno == ERANGE || integer > INT_MAX) {
            ATOMIC_PRINT({
                pwarn(
                    "integer is too large >> " invalidf("%s") " <<, limit is " validf("%d") ".",
                    intstr,
                    INT_MAX
                );
                perr();
                phelp_opts;
            });
            return -1;
        }

        *out++ = (id) ? (int32) FLIP_SIGN_BIT(integer) : integer;
    }

    return 0;
}


static int32 ashe_jobs(Command* cmd)
{
    static const char* name = namef(jobs) " - print currently running/stopped jobs.";
    static const char* usage = namef(jobs) " " obrack keywordf(PID) keywordf(|) keywordf(ID) cbrack;
    static const char* desc =
        namef(jobs) " - prints all currently running jobs and their status. "
        "This command can accept arguments in the form of " keywordf(PID) " or "
        keywordf(ID) " (job id). "
        "If the " keywordf(PID) " is supplied then the output is restricted " 
        "to jobs that contain the selected process ID. "
        "In case job " keywordf(ID) " is used, then the output will contain " 
        "only the job with the given job " keywordf(ID);

    Joblist* jlist = &ashe.sh_jlist;
    memmax len = cmd->argv.len;
    int32 status = 0;

    switch(len) {
        case 1: {
            memmax jobcnt = Joblist_len(jlist);
            if(jobcnt == 0) {
                builtin_printf("jobs", "there are no jobs running.");
                status = -1;
            } else {
                ashe_jobs_print_header();
                for(memmax i = 0; i < jobcnt; i++) {
                    Job* job = Joblist_getjob(jlist, i);
                    ashe_jobs_print_job(job);
                }
            }
            break;
        }
        case 2: {
            break;
        }
        default:
            break;
    }
    if(len == 2) {
        if(is_help_opt(ARGV(cmd, 1))) {
            print_manpage(name, usage, desc);
        } else {
            int32 integer = 0;
            if(get_integers(&cmd->argv, &integer, 1) == -1)
                status = -1;
            else {
                job_t* job = (integer < 0) ? joblist_find_id(&shell.sh_jlist, FLIP_SIGN_BIT(integer)) : joblist_find_pid(&shell.sh_jlist, integer);
                if(is_some(job)) {
                    ashe_jobs_print_header();
                    joboutf(job);
                    status = 0;
                } else
                    status = -1;
            }
        }
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

static int32 argv_len(char *const *argv)
{
    ssize_t len = 0;
    while(is_some(*argv++))
        len++;
    return len;
}

static void bg_last(void)
{
    job_t* job = joblist_get_fg_job(&shell.sh_jlist);
    if(is_some(job)) {
        job_continue(job, 0);
    } else
        ATOMIC_PRINT(PW_JOBMV("bg"));
}

static void bg_id(int32 id)
{
    job_t* job = joblist_get_fg_id(&shell.sh_jlist, id);

    if(is_some(job)) {
        job_continue(job, 0);
    } else
        ATOMIC_PRINT(PW_JOBMV_ID("bg", id));
}

static void bg_pid(pid_t pid)
{
    job_t* job = joblist_get_fg_pid(&shell.sh_jlist, pid);

    if(is_some(job))
        job_continue(job, 0);
    else
        ATOMIC_PRINT(PW_JOBMV_PID("bg", pid));
}

static int32 ashe_bg(char * const* argv)
{
    static const char* name = namef(bg) " - move jobs to background";
    static const char* usage = 
        namef(bg) " " obrack keywordf(PID) "..." cbrack "\n"
        namef(bg) " " obrack keywordf(%ID) "..." cbrack;
    static const char* desc = 
        namef(bg) " moves jobs to the background automatically resuming them if they were stopped.\n"
        "There are multiple ways of specifying which job to move, either by " keywordf(PID) ", " keywordf(ID) " or "
        "by not giving any arguments to the command. "
        "User can provide multiple " keywordf(PID) "s, if any job contains a process with that ID it will be put into background. "
        "Additionally the " keywordf(ID) " of a job can be used to move a jobs to background, this is done by specifying "
        validf("%") " in front of the " keywordf(ID) ", for example -> " validf("%5") keywordf(ID) ", this would place "
        "job with " keywordf(ID) " of " validf("5") " into the background. "
        "Finally by not specifying any arguments the last job that was used will be moved to background.";

    int32 status;
    memmax len = argv_len(argv);

    if(len == 1) {
        bg_last();
        status = 0;
    } else {
        if(len == 2 && is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = 0;
        } else {
            memmax n = argv_len(argv + 1);
            int32 integers[n];

            if(get_integers(argv + 1, integers, n) == -1) {
                status = -1;
            } else {
                for(memmax i = 0; i < n; i++)
                    (integers[i] < 0) ? bg_id(FLIP_SIGN_BIT(integers[i])) : bg_pid(integers[i]);
                status = 0;
            }
        }
    }

    return status;
}

static void fg_id(int32 id)
{
    job_t* job = joblist_get_bg_id(&shell.sh_jlist, id);

    if(is_some(job))
        job_continue(job, 1);
    else
        ATOMIC_PRINT(PW_JOBMV_ID("fg", id));
}

static void fg_pid(pid_t pid)
{
    job_t* job = joblist_get_bg_pid(&shell.sh_jlist, pid);

    if(is_some(job))
        job_continue(job, 1);
    else
        ATOMIC_PRINT(PW_JOBMV_PID("fg", pid));
}

static void fg_last(void)
{
    job_t* job = joblist_get_bg_job(&shell.sh_jlist);

    if(is_some(job)) {
        job_continue(job, 1);
    } else
        ATOMIC_PRINT(PW_JOBMV("fg"));
}

static int32 ashe_fg(char* const* argv)
{
    static const char* name = namef(fg) " - move job to foreground";
    static const char* usage = 
        namef(fg) " " obrack keywordf(PID) cbrack "\n"
        namef(fg) " " obrack keywordf(%ID) cbrack;
    static const char* desc =
        namef(fg) " bring the specified job to the foreground additionally resuming it if was stopped. "
        "If no arguments were supplied then the last job that was used is put into foreground. "
        "In case " keywordf(PID) " was provided, then the job that contains specified process ID is put in the foreground. "
        "Additionally the " keywordf(ID) " of a job can be used given that user provides " validf("%") " before it, "
        validf("%5")", this would try to bring the foreground job with " keywordf(ID) " of " validf("5") " into foreground.";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        fg_last();
        status = 0;
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = 0;
        } else {
            int32 integer;
            if((status = get_integers(argv + 1, &integer, 1)) != -1)
                (integer < 0) ? fg_id(FLIP_SIGN_BIT(integer)) : fg_pid(integer);
        }
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

ubyte is_builtin(const char *command)
{
    for(memmax i = 0; i < BUILTINN; i++)
        if(strcmp(command, builtin[i]) == 0)
            return 1;
    return 0;
}

static int32 exit_builtin(char *const *argv, ubyte is_shell)
{
    static const char* usage = namef(exit) " " obrack keywordf(CODE) cbrack;
    static const char* name = namef(exit) " - exits the shell";
    static const char* desc = 
        namef(exit) " is a builtin command that exits the shell with the " keywordf(CODE) 
        " if supplied or 0 in case " keywordf(CODE) " is not supplied. "
        "In case there are jobs running when exit is called, shell will not exit instantly, "
        "instead it will warn user first if there are any running jobs.";
    static const char *exit_warn = 
        "There are background jobs that are still running/stopped! "
        "Run " namef(exit) " again to exit, this will result in " invalidf("termination") " of child processes.";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        if(is_shell) {
            if(joblist_len(&shell.sh_jlist) != 0 && !exit_warning) {
                exit_warning = 1;
                ATOMIC_PRINT(pwarn("%s", exit_warn));
            } else
                exit(0);
        }
        status = 0;
    } else if(len == 2) {
        int32 exit_status;
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = 0;
        } else {
            exit_status = strtol(argv[1], NULL, 10);
            if(errno == EINVAL || errno == ERANGE) {
                ATOMIC_PRINT({
                    pwarn("invalid exit status integer " invalidf("%s"), argv[1]);
                    phelp_opts;
                });
                status = -1;
            } else {
                status = exit_status;
                if(is_shell) {
                    if(joblist_len(&shell.sh_jlist) != 0 && !exit_warning) {
                        exit_warning = 1;
                        ATOMIC_PRINT(pwarn("%s", exit_warn));
                    } else
                        exit(exit_status);
                }
            }
        }
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
        exit_warning = 0;
    }

    return status;
}

static int32 ashe_change_dir(char *const *argv)
{
    static const char *usage = namef(cd) " " obrack keywordf(DIRNAME) cbrack;
    static const char *name = namef(cd) " - change directory";
    static const char *desc = 
        namef(cd) " command changes current working directory. "
        "If no " keywordf(DIRECTORY) " is given, then the " keywordf(HOME) " environment variable will be used. "
        "Otherwise " keywordf(DIRECTORY) " becomes the new working directory.";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        if(__glibc_unlikely((status = chdir(getenv(HOME))) == -1))
            ATOMIC_PRINT(perr());
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = 0;
        } else {
            status = chdir(argv[1]);
            if(status < 0) {
                if(errno == ENOENT)
                    PW_NOPATH(argv[1]);
                perr();
            }
        }
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

static int32 ashe_print_envvar(char *const *argv)
{
    static const char *name = namef(penv) " - print environment variable/s.";
    static const char *usage = namef(penv) " " obrack keywordf(NAME) cbrack;
    static const char *desc = 
        namef(penv) " prints environmental variables. "
        "If no " keywordf(NAME) " is provided " namef(penv) " will print all environment variables. "
        "If " keywordf(NAME) " is provided, " namef(penv) " prints the value of that variable with that "
        keywordf(NAME) ".";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        status = envcmd(NULL, P_ALL);
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = 0;
        } else
            status = envcmd(argv, P_ENV);
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

int32 ashe_set_envvar(char *const *argv)
{
    static const char *name = namef(senv) " - set environment variable.";
    static const char *usage = 
        namef(senv) " " obrack keywordf(NAME) cbrack " " obrack keywordf(VALUE) cbrack;
    static const char *desc = 
        namef(penv) " set/add environmental variable. "
        "If variable with " keywordf(NAME) " already exists then its " keywordf(VALUE) " is overwritten. "
        "In case the variable with " keywordf(NAME) " doesn't already exist, then it is newly created and "
        "added into the environment with the value of " keywordf(VALUE) ". "
        "Additionally if the " keywordf(NAME) " was provided but " keywordf(VALUE) " was not, then the "
        "variable is initialized with the value of empty string (" keywordf("") ").";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        ATOMIC_PRINT({
            PW_FEWARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = 0;
        } else  
            status = envcmd(argv, SET_ENV);
    } else if(len == 3) {
        status = envcmd(argv, SET_ENV);
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

static int32 ashe_remove_envvar(char *const *argv)
{
    static const char *name = namef(renv) " - remove environment variable.";
    static const char *usage = namef(renv) " " obrack keywordf(NAME);
    static const char *desc = 
        namef(renv) " removes environmental variable. "
        "If variable with " keywordf(NAME) " exists it gets removed from the environment.";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        ATOMIC_PRINT({
            PW_FEWARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = 0;
        } else
            status = envcmd(argv, RM_ENV);
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

static int32 envcmd(char *const *argv, int32 option)
{
    const char *temp = NULL;
    int32 status = -1;

    switch(option) {
        case ADD_ENV:
        case SET_ENV:
            temp = (is_null(argv[2])) ? "" : argv[2];
            option = (option == ADD_ENV) ? 0 : 1;
            if(__glibc_unlikely((status = setenv(argv[1], temp, option)) < 0))
                ATOMIC_PRINT(perr());
            break;
        case RM_ENV: status = unsetenv(argv[1]); break;
        case P_ENV:
            if(is_some(temp = getenv(argv[1]))) {
                ATOMIC_PRINT(printf("%s\n", temp));
                status = 0;
            }
            break;
        case P_ALL:
            ATOMIC_PRINT(penviron());
            status = 0;
            break;
        default: break;
    }

    return status;
}

static void penviron(void)
{
    int32 i = 0;
    while(is_some(environ[i]))
        ATOMIC_PRINT(printf("%s\n", environ[i++]));
}

static int32 ashe_pwd(char * const * argv)
{
    static const char* name = namef(pwd) " - print the current working directory.";
    static const char* usage = namef(pwd);
    static const char* desc = namef(pwd) " prints the current working directory. (ashe builtin pwd)";

    int32 status;
    ssize_t len = argv_len(argv);
    char buff[PATH_MAX];

    if(len == 1) {
        if(__glibc_unlikely(is_null(getcwd(buff, PATH_MAX)))) {
            ATOMIC_PRINT(perr());
            status = -1;
        } else {
            ATOMIC_PRINT(printf("%s\n", buff));
            status = 0;
        }
    } else if(len == 2 && is_help_opt(argv[1])) {
        status = 0;
        ATOMIC_PRINT(pmanpage(name, usage, desc));
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

static int32 ashe_clear(char* const* argv)
{
    static const char* name = namef(clear) " - clear the screen.";
    static const char* usage = namef(clear);
    static const char* desc = namef(clear) " - clears the terminal screen including scrollback. (ashe builtin)";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        write_or_die(clrscr mv_cur_home, sizeof(clrscr mv_cur_home));
        status = 0;
    } else if(len == 2 && is_help_opt(argv[1])) {
        ATOMIC_PRINT(pmanpage(name, usage, desc));
        status = 0;
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}

static int32 ashe_builtin(char* const* argv)
{
    static const char* name = namef(builtin) " - print list of all builtin commands.";
    static const char* usage = namef(builtin);
    static const char* desc = namef(builtin) " prints all ashe builtin commands. (ashe builtin)";

    int32 status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        pbuiltin();
        status = 0;
    } else if(len == 2 && is_help_opt(argv[1])) {
        ATOMIC_PRINT(pmanpage(name, usage, desc));
        status = 0;
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            phelp_opts;
        });
        status = -1;
    }

    return status;
}


/* Auxiliary to 'exec'. */
int32 exec_file_descriptors(ArrayFDContext* fdcs)
{
    FDContext* fdc = NULL;
    for(memmax i = 0; i < fdcs->len; i++) {
        fdc = ArrayFDContext_index(fdcs, i);
        if(fdc->close) { // close file descriptor ?
            if(!fd_isvalid(fdc->fd_left) || unlikely(close(fdc->fd_left) < 0)) 
                goto l_error;
        } else { // open file descriptor
            if(!fd_isvalid(fdc->fd_right)) goto l_error;
            if(!fd_isvalid(fdc->fd_left)) {

            }
        }
    }
    return 0;
l_error:
    print_errno();
    return -1;
}

static int32 ashe_exec(Command* cmd)
{

}


static void pbuiltin(void)
{
    ATOMIC_PRINT({
        for(memmax i = 0; i < BUILTINN; i++)
            printf("%s\n", builtin[i]);
    });
}

int32 run_builtin(const char *command, char *const *argv, ubyte shell)
{
    int32 status = 0;

    if(strcmp(command, "exit") == 0) return exit_builtin(argv, shell);
    else exit_warning = 0;

    /* TODO: rope data structure */
    if(strcmp(command, "cd") == 0) {
        status = ashe_change_dir(argv);
    } else if(strcmp(command, "penv") == 0) {
        status = ashe_print_envvar(argv);
    } else if(strcmp(command, "senv") == 0) {
        status = ashe_set_envvar(argv);
    } else if(strcmp(command, "renv") == 0) {
        status = ashe_remove_envvar(argv);
    } else if(strcmp(command, "pwd") == 0) {
        status = ashe_pwd(argv);
    } else if(strcmp(command, "clear") == 0) {
        status = ashe_clear(argv);
    } else if(strcmp(command, "builtin") == 0) {
        status = ashe_builtin(argv);
    } else if(strcmp(command, "fg") == 0) {
        status = ashe_fg(argv);
    } else if(strcmp(command, "bg") == 0) {
        status = ashe_bg(argv);
    } else if(strcmp(command, "jobs") == 0) {
        status = ashe_jobs(argv);
    } else if(strcmp(command, "exec") == 0) {
        status = ashe_exec(argv);
    } else {
        fprintf(stderr, "FIX ME: UNREACHABLE CODE\nFILE: %s\nLINE: %d\nFUNCTION: %s\n", __FILE__, __LINE__, __func__);
        exit(EXIT_FAILURE);
    }

    return status;
}
