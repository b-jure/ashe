#ifndef __ASH_ERRORS_H__
#define __ASH_ERRORS_H__

#include "input.h"
#include "ashe_utils.h"

#define PW_FGID_ERR(id)                                                        \
  pwarn("There is no suitable job with id " bold(                              \
            bred("%d")) " to move into foreground!",                           \
        id)

#define PW_FGPGID_ERR(pgid)                                                    \
  pwarn("There is no suitable job with pgid " bold(                            \
            bred("%d")) " to move into foreground!",                           \
        pgid)

#define PW_FG_ERR pwarn("There is no suitable job to move into foreground!")

#define PW_OOM(bytes)                                                          \
  pwarn("ran out of memory while trying to allocate " bold(                    \
            bred("%ld")) " bytes",                                             \
        (bytes))

#define PW_JLINIT pwarn("failed creating a joblist")

#define PW_STATVAR_INIT                                                        \
  pwarn("failed creating status environment variable" bold(bred("?")))

#define PW_SHCLEANUP_INIT pwarn("failed setting up shell cleanup routine")

#define PW_NOPATH(ptr) pwarn("couldn't find the path " bold(bred("%s")), (ptr))

#define PW_NOFILE(ptr) pwarn("couldn't find the file " bold(bred("%s")), (ptr))

#define PW_EXECERR(prog)                                                       \
  pwarn("failed while trying to run command " bold(bred("%s")), (prog))

#define PW_PGRPSET(pid, pgid)                                                  \
  pwarn(obrack bold(blue("P_%d")) cbrack                                       \
        " failed setting itself into process group " bold(blue("%d")),         \
        (pid), (pgid))

#define PW_TERMTCSET(pgid)                                                     \
  pwarn("failed giving terminal to process group " obrack bold(blue("P_%d"))   \
            cbrack,                                                            \
        (pgid))

#define PW_FORKERR                                                             \
  pwarn("failed forking parent process " obrack bold(blue("P_%d")) cbrack,     \
        getpid())

#define PW_SHDUP pwarn("failed restoring shell streams")

#define PW_SHDFLMODE pwarn("failed restoring shell's terminal modes")

#define PW_COPYERR(ptr)                                                        \
  pwarn("failed copying over the user input: " bold(bred("%s")), (ptr))

#define PW_VARRM(varp)                                                         \
  pwarn("failed removing variable" bold(bred(" %s")), *(varp))

#define PW_VAREXPO(varp)                                                       \
  pwarn("failed exporting variable" bold(bred(" %s")), *(varp))

#define PW_ADDJ(job)                                                           \
  pwarn("failed adding a job " obrack bold(byellow("J_%d")) cbrack             \
        " to the joblist",                                                     \
        (job)->pgid)

#define PW_CPIPE(pptr)                                                         \
  pwarn("failed closing a pipe " obrack bold(magenta("PW_%d")) bold(cyan("|")) \
            bold(magenta("RP_%d")) cbrack,                                     \
        *(pptr), *((pptr) + 1))

#define PW_MANYARG(prog)                                                       \
  pwarn("too many arguments provided for command '%s'", (prog))

#define PW_FEWARG(prog) pwarn("missing argument/s for command '%s'", (prog))

#define PW_OPENF(prog) pwarn("failed opening a file " bold(bred("%s")), (prog))

#define PW_CLOSEF(fd)                                                          \
  pwarn("failed closing a file with file descriptor " bold(bred("%d")), (fd))

#define PW_PROCTOJOB(pid)                                                      \
  pwarn("failed adding process " obrack bold(blue("P_%d")) cbrack " to job",   \
        (pid))

#define PW_TERMINR pwarn("failed reading terminal input")

#define PW_USERNAME pwarn("failed fetching username")

#define PW_SYSNAME pwarn("failed fetching system name")

#define PW_SIGINIT pwarn("failed setting up shell signal handlers")

#define PW_SIGINITS(signalp)                                                   \
  pwarn("failed setting up shell" bold(bred(" %s ")) "signal handler",         \
        (signalp))

#define PW_PARSINVALTOK(token_contents)                                        \
  pwarn("expected" bold(green(" && ")) "or" bold(                              \
            green(" || ")) "instead got" bold(bred(" %s")),                    \
        (token_contents))

#endif
