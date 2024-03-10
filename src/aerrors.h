#ifndef AERRORS_H
#define AERRORS_H

#include "autils.h"
#include "ainput.h"

#define PW_SYNTAX_EOL                                                                              \
    print_warning("expected " bold(green("command/var")) " instead got " bold(bred("eol")) ".")

#define PW_SYNTAX_GENERIC() print_warning("expected " bold(green("command/var")) ".")

#define PW_SYNTAX(str)                                                                             \
    print_warning("expected " bold(green("command/var")) " instead got " bold(bred("%s")) ".", str)

#define PW_JOBMV_ID(prog, id)                                                                      \
    print_warning(bold(bred("%s")) ": there is no suitable job with ID " bold(bred("%d")), prog, id)
#define PW_JOBMV_PID(prog, pid)                                                                    \
    print_warning(                                                                                 \
        bold(bred("%s")) ": there is no suitable job containing PID " bold(bred("%d")),            \
        prog,                                                                                      \
        pid)

#define PW_JOBMV(prog) print_warning(bold(bred("%s")) ": there is no suitable job!", prog)

#define PW_OOM(bytes)                                                                              \
    print_warning("ran out of memory while trying to allocate " bold(bred("%ld")) " bytes", (bytes))

#define PW_JLINIT print_warning("failed creating a joblist")

#define PW_SHCLEANUP_INIT print_warning("failed setting up shell cleanup routine")

#define PW_NOPATH(ptr) print_warning("couldn't find the path " bold(bred("%s")), (ptr))

#define PW_NOFILE(ptr) print_warning("couldn't find the file " bold(bred("%s")), (ptr))

#define PW_EXECERR(prog)                                                                           \
    print_warning("failed while trying to run command " bold(bred("%s")), (prog))

#define PW_PGRPSET(pid, pgid)                                                                      \
    print_warning(                                                                                 \
        obrack bold(blue("P_%d")) cbrack                                                           \
        " failed setting itself into process group " bold(blue("%d")),                             \
        (pid),                                                                                     \
        (pgid))

#define PW_TERMTCSET(pgid)                                                                         \
    print_warning(                                                                                 \
        "failed giving terminal to process group " obrack bold(blue("P_%d")) cbrack,               \
        (pgid))

#define PW_FORKERR                                                                                 \
    print_warning("failed forking parent process " obrack bold(blue("P_%d")) cbrack, getpid())

#define PW_SHDUP print_warning("failed restoring shell streams")

#define PW_SHDFLMODE print_warning("failed restoring shell's terminal modes")

#define PW_VARRM(varp) print_warning("failed removing variable" bold(bred(" %s")), *(varp))

#define PW_VAREXPO(varp) 

#define PW_ADDJ(job)                                                                               \
    print_warning(                                                                                 \
        "failed adding a job " obrack bold(byellow("J_%d")) cbrack " to the joblist",              \
        (job)->pgid)

#define PW_CPIPE(pptr)                                                                             \
    print_warning(                                                                                 \
        "failed closing a pipe " obrack bold(magenta("PW_%d")) bold(cyan("|"))                     \
            bold(magenta("RP_%d")) cbrack,                                                         \
        *(pptr),                                                                                   \
        *((pptr) + 1))

#define PW_MANYARG(prog) print_warning("too many arguments provided for command '%s'", (prog))

#define PW_FEWARG(prog) print_warning("missing argument/s for command '%s'", (prog))

#define PW_OPENF(prog) print_warning("failed opening a file " bold(bred("%s")), (prog))

#define PW_CLOSEF(fd)                                                                              \
    print_warning("failed closing a file with file descriptor " bold(bred("%d")), (fd))

#define PW_PROCTOJOB(pid)                                                                          \
    print_warning("failed adding process " obrack bold(blue("P_%d")) cbrack " to job", (pid))

#define PW_TERMINR print_warning("failed reading terminal input")

#define PW_USERNAME print_warning("failed fetching username")

#define PW_SYSNAME print_warning("failed fetching system name")

#define PW_SIGINIT print_warning("failed setting up shell signal handlers")

#define PW_SIGINITS(signalp)                                                                       \
    print_warning("failed setting up shell" bold(bred(" %s ")) "signal handler", (signalp))

#define PW_PARSINVALTOK(token_contents)                                                            \
    print_warning("syntax error near token contents " bold(bred(" %s")), (token_contents))

#endif
