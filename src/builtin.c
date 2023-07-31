#include "async.h"
#include "builtin.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

/// Options for envcmd function
#define ADD_ENV 0 /* Add env var name/value */
#define SET_ENV 1 /* Add/overwrite env var name/value */
#define RM_ENV  2 /* Remove env var */
#define P_ENV   3 /* Print env var */
#define P_ALL   4 /* Print all environ */

#define is_help_opt(arg) (strcmp((arg), "-h") == 0 || strcmp((arg), "--help") == 0)
#define phelp_opts                                                                                 \
    fprintf(stderr, "\r\nThe -h or --help options display help information for this command\n\r")

/// Built-in commands
byte *builtin[] = {
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
};

#define BUILTINN sizeof(builtin) / sizeof(builtin[0])

static int argv_len(byte *const *argv);
static void pbuiltin(void);
static void penviron(void);
static int envcmd(byte *const *argv, int option);
static int exit_builtin(byte *const *argv, bool shell);
static int change_dir(byte *const *argv);
static int print_variable(byte *const *argv);
static int remove_variable(byte *const *argv);
static int pwd(byte *const *argv);
static int clear(byte *const *argv);
static int builtin_names(byte *const *argv);
static int get_integers(byte *const *argv, int32_t *out, size_t len);
static void fg_last(void);
static void fg_pid(pid_t pid);
static void fg_id(int32_t id);
static int fg(byte *const *argv);
static void bg_last(void);
static void bg_id(int32_t id);
static void bg_pid(pid_t pid);
static int bg(byte *const *argv);

/// This is used in 'bg' builtin command when parsing and storing combination
/// of id and pid integers in the same array to later distinguish them by their sign bit.
/// All valid PID's and ID's are positive so by flipping a sign bit we can differentiate
/// them while also storing them in the same array without storing each inside a tagged union.
#define FLIP_SIGN_BIT(integer)                                                                     \
    ((int32_t) ((int32_t) (integer)) ^ (~((uint32_t) 0) << ((sizeof(uint32_t) * 8) - 1)))

// clang-format off

static int argv_len(byte *const *argv)
{
    ssize_t len = 0;
    while(is_some(*argv++))
        len++;
    return len;
}

static int get_integers(byte* const* argv, int32_t *out, size_t len)
{
    byte *intstr;
    byte *errstr;
    bool id = false;
    int integer;
    size_t precision;

    while(len--) {
        intstr = *argv;
        if(*intstr == '%') {
            id = true;
            if(*++intstr == '\0') {
                ATOMIC_PRINT({
                    pwarn("provided '%' but missing ID");
                    phelp_opts;
                });
                return FAILURE;
            }
        }

        integer = strtol(intstr, &errstr, 10);

        if(*errstr != '\0') {
            precision = errstr - intstr;
            ATOMIC_PRINT({
                pwarn(
                    "invalid integer provided >> " bold(blue("%*s")) bold(red("%s")) " <<",
                    precision,
                    intstr,
                    errstr
                );
                phelp_opts;
            });
            return FAILURE;
        } else if(errno == ERANGE || integer > INT_MAX) {
            ATOMIC_PRINT({
                pwarn(
                    "integer is too large >> " bold(red("%s")) " <<, limit is " bold(blue("%d")) ".",
                    intstr,
                    INT_MAX
                );
                perr();
                phelp_opts;
            });
            return FAILURE;
        }

        *out++ = (id) ? (int32_t) FLIP_SIGN_BIT(integer) : integer;
    }

    return SUCCESS;
}

static void bg_last(void)
{
    if(last_bg == NULL)
        ATOMIC_PRINT(PW_JOBMV("bg"));
    else
        job_continue(last_bg, false);
}

static void bg_id(int32_t id)
{
    job_t* job;

    job = joblist_find_id(id);

    if(is_some(job))
        job_continue(job, false);
    else
        ATOMIC_PRINT(PW_JOBMV_ID("bg", id));
}

static void bg_pid(pid_t pid)
{
    job_t* job;

    job = joblist_find_pid(pid);

    if(is_some(job))
        job_continue(job, false);
    else
        ATOMIC_PRINT(PW_JOBMV_PID("bg", pid));
}

static int bg(byte * const* argv)
{
    static const byte* name = bold(green("bg")) " - move jobs to background";
    static const byte* usage = 
        bold(green("bg")) " " obrack italic("PID") "..." cbrack"\r\n"
        "\t" bold(green("bg")) " " obrack italic("%ID") "..." cbrack;
    static const byte* desc = 
        bold(green("bg")) " moves jobs to the background automatically resuming them if they were stopped\n\r"
        "\tThere are multiple ways of specifying which job to move, either by " italic("PID") ", " italic("ID") " or "
        "by not giving any arguments to the command.\r\n"
        "\tUser can provide multiple " italic("PID") "s, if any job contains a process with that ID it will be put into "
        "background.\r\n"
        "\tAdditionally the " italic("ID") " of a job can be used to move a jobs to background, this is done by specifying "
        bold(blue("%")) " in front of the " italic("ID") ", for example -> " bold(blue("%5")) italic("ID") ", this would place "
        " job with " italic("ID") " of " bold(blue("5")) " into the background.\r\n"
        "\tFinally by not specifying any arguments the last job that was in the foreground will be moved to background.";

    int status;
    size_t len = argv_len(argv);

    if(len == 1) {
        bg_last();
        status = SUCCESS;
    } else {
        if(len == 2 && is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = SUCCESS;
        } else {
            size_t n = argv_len(argv + 1);
            int32_t integers[n];

            if(get_integers(argv + 1, integers, n) == FAILURE) {
                status = FAILURE;
            } else {
                for(size_t i = 0; i < n; i++)
                    (integers[i] < 0) ? bg_id(FLIP_SIGN_BIT(integers[i])) : bg_pid(integers[i]);
                status = SUCCESS;
            }
        }
    }

    return status;
}

static void fg_id(int32_t id)
{
    job_t* job = joblist_find_id(id);

    if(is_some(job))
        job_continue(job, true);
    else
        ATOMIC_PRINT(PW_JOBMV_ID("fg", id));
}

static void fg_pid(pid_t pid)
{
    job_t* job = joblist_find_pid(pid);

    if(is_some(job))
        job_continue(job, true);
    else
        ATOMIC_PRINT(PW_JOBMV_PID("fg", pid));
}

static void fg_last(void)
{
    if(is_some(last_fg))
        job_continue(last_fg, true);
    else
        ATOMIC_PRINT(PW_JOBMV("fg"));
}

static int fg(byte* const* argv)
{
    static const byte* name = bold(green("fg")) " - move job to foreground";
    static const byte* usage = 
        bold(green("fg")) " " obrack italic("PID") cbrack "\r\n"
        "\t" bold(green("fg")) " " obrack italic("%ID") cbrack;
    static const byte* desc =
        bold(green("fg")) " bring the specified job to the foreground additionally resuming it if was stopped\r\n"
        "\tIf no arguments were supplied then the last job to be created it put into foreground.\r\n"
        "\tIn case " italic("PID") " was provided, then the job that contains specified process ID is put "
        "in the foreground.\r\n"
        "\tAdditionally the " italic("ID") " of a job can be used given that user provides " bold(blue("%")) " before it, "
        bold(blue("%5"))", this would try to bring the foreground job with " italic("ID") " of " bold(blue("5")) " into foreground.\r\n";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        fg_last();
        status = SUCCESS;
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = SUCCESS;
        } else {
            int32_t integer;
            if((status = get_integers(argv + 1, &integer, 1)) != FAILURE)
                (integer < 0) ? fg_id(FLIP_SIGN_BIT(integer)) : fg_pid(integer);
        }
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

bool is_builtin(const byte *command)
{
    for(size_t i = 0; i < BUILTINN; i++)
        if(strcmp(command, builtin[i]) == 0)
            return true;
    return false;
}

static int exit_builtin(byte *const *argv, bool shell)
{
    static const byte* usage = bold(green("exit")) " " obrack italic("CODE") cbrack;
    static const byte* name = bold(green("exit")) " - exits the shell";
    static const byte* desc = 
        bold(green("exit")) " is a builtin command that exits the shell with the " italic("CODE") 
        " if supplied or 0 in case " italic("CODE") " is not supplied.\r\n"
        "\tIn case there are background jobs running when exit is called, shell will not exit instantly, "
        "instead it will warn user first if there are any running/stopped background jobs.";
    static const byte *exit_warn = 
        "There are background jobs that are still running/stopped!\n\r"
        "Run " bold(green("exit"))" again to exit, this will result in " bold(bred("termination")) 
        " of child processes.";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        if(shell) {
            if(joblist_len() != 0 && !exit_warning) {
                exit_warning = true;
                ATOMIC_PRINT(pwarn("%s", exit_warn));
            } else
                exit(SUCCESS);
        }
        status = SUCCESS;
    } else if(len == 2) {
        int exit_status;
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = SUCCESS;
        } else {
            exit_status = strtol(argv[1], NULL, 10);
            if(errno == EINVAL || errno == ERANGE) {
                ATOMIC_PRINT({
                    pwarn("invalid exit status integer " bold(bred("%s")), argv[1]);
                    pinfo(INF_USAGE, "%s", usage);
                    phelp_opts;
                });
                status = FAILURE;
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
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
        exit_warning = false;
    }

    return status;
}

static int change_dir(byte *const *argv)
{
    static const byte *usage = bold(green("cd")) " " obrack italic("DIRNAME") cbrack;
    static const byte *name = bold(green("cd")) " - change directory";
    static const byte *desc = 
        bold(green("cd")) " command changes current working directory.\r\n"
        "\tIf no " italic("DIRECTORY") " is given, then the " italic("HOME")
        " environment variable will be used.\r\n"
        "\tOtherwise " italic("DIRECTORY") " becomes the new working directory.";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        if(__glibc_unlikely((status = chdir(getenv(HOME))) == FAILURE))
            ATOMIC_PRINT(perr());
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = SUCCESS;
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
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

static int print_variable(byte *const *argv)
{
    static const byte *name = bold(green("penv")) " - print environment variable/s.";
    static const byte *usage = bold(green("penv")) " " obrack italic("NAME") cbrack;
    static const byte *desc = 
        bold(green("penv")) " prints environmental variables.\n\r"
        "\tIf no " italic("NAME") " is provided " green("penv") " will print all environment variables.\r\n"
        "\tIf " italic("NAME") " is provided, " green("penv") " prints the value of that variable with that "
        italic("NAME") ".";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        status = envcmd(NULL, P_ALL);
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = SUCCESS;
        } else
            status = envcmd(argv, P_ENV);
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

int set_variable(byte *const *argv)
{
    static const byte *name = bold(green("senv")) " - set environment variable.";
    static const byte *usage = 
        bold(green("senv")) " " obrack italic("NAME") cbrack " " obrack italic("VALUE") cbrack;
    static const byte *desc = 
        bold(green("penv")) " set/add environmental variable.\n\r"
        "\tIf variable with " italic("NAME") " already exists then its " italic("VALUE") " is overwritten.\r\n"
        "\tIn case the variable with " italic("NAME") " doesn't already exist, then it is newly created and "
        "added into the environment with the value of " italic("VALUE") ".\r\n"
        "\tAdditionally if the " italic("NAME") " was provided but " italic("VALUE") " was not, then the "
        "variable is initialized with the value of empty string (" italic("\"\"") ").";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        ATOMIC_PRINT({
            PW_FEWARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = SUCCESS;
        } else  
            status = envcmd(argv, SET_ENV);
    } else if(len == 3) {
        status = envcmd(argv, SET_ENV);
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

static int remove_variable(byte *const *argv)
{
    static const byte *name = bold(green("renv")) " - remove environment variable.";
    static const byte *usage
        = bold(green("renv")) " " obrack italic("NAME");
    static const byte *desc = 
        bold(green("renv")) " removes environmental variable.\n\r"
        "\tIf variable with " italic("NAME") " exists it gets removed from the environment.";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        ATOMIC_PRINT({
            PW_FEWARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    } else if(len == 2) {
        if(is_help_opt(argv[1])) {
            ATOMIC_PRINT(pmanpage(name, usage, desc));
            status = SUCCESS;
        } else
            status = envcmd(argv, RM_ENV);
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

static int envcmd(byte *const *argv, int option)
{
    const byte *temp = NULL;
    int status = FAILURE;

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
                ATOMIC_PRINT(printf("%s\n\r", temp));
                status = SUCCESS;
            }
            break;
        case P_ALL:
            ATOMIC_PRINT(penviron());
            status = SUCCESS;
            break;
        default: break;
    }

    return status;
}

static void penviron(void)
{
    int i = 0;
    while(is_some(environ[i]))
        ATOMIC_PRINT(printf("%s\r\n", environ[i++]));
}

static int pwd(byte * const * argv)
{
    static const byte* name = bold(green("pwd")) " - print the current working directory.";
    static const byte* usage = bold(green("pwd"));
    static const byte* desc = bold(green("pwd")) " prints the current working directory. (ashe builtin pwd)";

    int status;
    ssize_t len = argv_len(argv);
    byte buff[PATH_MAX];

    if(len == 1) {
        if(__glibc_unlikely(is_null(getcwd(buff, PATH_MAX)))) {
            ATOMIC_PRINT(perr());
            status = FAILURE;
        } else {
            ATOMIC_PRINT(printf("%s\n\r", buff));
            status = SUCCESS;
        }
    } else if(len == 2 && is_help_opt(argv[1])) {
        status = SUCCESS;
        ATOMIC_PRINT(pmanpage(name, usage, desc));
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

static int clear(byte* const* argv)
{
    static const byte* name = bold(green("clear")) " - clear the screen.";
    static const byte* usage = bold(green("clear"));
    static const byte* desc = bold(green("clear")) " - clears the terminal screen including scrollback. (ashe builtin)";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        write_or_die(clrscr mv_cur_home, sizeof(clrscr mv_cur_home));
        status = SUCCESS;
    } else if(len == 2 && is_help_opt(argv[1])) {
        ATOMIC_PRINT(pmanpage(name, usage, desc));
        status = SUCCESS;
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

static int builtin_names(byte* const* argv)
{
    static const byte* name = bold(green("builtin")) " - print list of all builtin commands.";
    static const byte* usage = bold(green("builtin"));
    static const byte* desc = bold(green("builtin")) " prints all ashe builtin commands. (ashe builtin)";

    int status;
    ssize_t len = argv_len(argv);

    if(len == 1) {
        pbuiltin();
        status = SUCCESS;
    } else if(len == 2 && is_help_opt(argv[1])) {
        ATOMIC_PRINT(pmanpage(name, usage, desc));
        status = SUCCESS;
    } else {
        ATOMIC_PRINT({
            PW_MANYARG(argv[0]);
            pinfo(INF_USAGE, "%s", usage);
            phelp_opts;
        });
        status = FAILURE;
    }

    return status;
}

static void pbuiltin(void)
{
    for(size_t i = 0; i < BUILTINN; i++)
        printf("%s\n\r", builtin[i]);
}

int run_builtin(const byte *command, byte *const *argv, bool shell)
{
    int status = SUCCESS;

    if(strcmp(command, "exit") == 0)
        return exit_builtin(argv, shell);
    else
        exit_warning = false;

    if(strcmp(command, "cd") == 0) {
        status = change_dir(argv);
    } else if(strcmp(command, "penv") == 0) {
        status = print_variable(argv);
    } else if(strcmp(command, "senv") == 0) {
        status = set_variable(argv);
    } else if(strcmp(command, "renv") == 0) {
        status = remove_variable(argv);
    } else if(strcmp(command, "pwd") == 0) {
        status = pwd(argv);
    } else if(strcmp(command, "clear") == 0) {
        status = clear(argv);
    } else if(strcmp(command, "builtin") == 0) {
        status = builtin_names(argv);
    } else if(strcmp(command, "fg") == 0) {
        status = fg(argv);
    } else if(strcmp(command, "bg") == 0) {
        status = bg(argv);
    } else {
        fprintf(stderr, "FIX ME: UNREACHABLE CODE\r\nFILE: %s\n\rLINE: %d\r\nFUNCTION: %s\n\r", __FILE__, __LINE__, __func__);
        exit(EXIT_FAILURE);
    }

    return status;
}
