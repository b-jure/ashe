// clang-format off
#ifndef __ASH_UTILS_H__
#define __ASH_UTILS_H__

typedef char byte;

#define is_null(ptr) ((ptr) == NULL)
#define is_some(ptr) ((ptr) != NULL)
#define EOL '\0'

#define PCS_EXTRA "/.-"
#define PORTABLE_CHARACTER_SET \
    "0123456789_qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM"

#if defined(__unix__) || defined(__linux__)
    #define PATH_DELIM ':'
#elif defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
    #define PATH_DELIM ';'
#endif

#if defined(_WIN32) || defined(_WIN64)
    #define ARG_MAX 32767 // Windows
#elif defined(__CYGWIN__) && !defined(_WIN32)
    #include <limits.h>// Windows (Cygwin POSIX under Microsoft Window)
#elif defined(__linux__)
    #include <linux/limits.h> // Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Centos and other
#elif defined(__unix__) || !defined(__APPLE__) && defined(__MACH__)
    #include <limits.h>
    #if defined(BSD)
        #include <sys/syslimits.h> // FreeBSD, NetBSD, OpenBSD, DragonFly BSD
    #endif
#elif defined(__hpux)
    #include <limits.h> // HP-UX
#elif defined(_AIX)
    #include <sys/limits.h> // IBM AIX
#elif defined(__sun) && defined(__SVR4)
    #include <limits.h> // Oracle Solaris, Open Indiana
#else
    #undef ARG_MAX
    #undef PATH_MAX
#endif

#if !defined(ARG_MAX) || !defined(PATH_MAX)
    #define _POSIX_SOURCE
    #include <limits.h>
    #ifndef ARG_MAX
        #define ARG_MAX _POSIX_ARG_MAX
    #endif
    #ifndef PATH_MAX
        #define PATH_MAX _POSIX_PATH_MAX
    #endif
#endif

// clang-format on

/// ERRORS
#define EXPECTED_STRING_ERR(str)                                               \
  fprintf(stderr, "anshell: expected a string instead got '%s'\n", str);
#define EXPECTED_CMD_ERR(str)                                                  \
  fprintf(stderr, "anshell: expected a command instead got '%s'\n", str);
#define OOM_ERR(bytes)                                                         \
  fprintf(stderr, "anshell - out of memory, tried to allocated %ld bytes\n",   \
          bytes)
#define ARG_SIZE_ERR(size)                                                     \
  fprintf(stderr, "anshell: maximum arg size of %lub exceeded\n", size - 1)
#define EXPECTED_STREOL_GOT_REDIR_ERR(str)                                     \
  fprintf(stderr,                                                              \
          "anshell: expected a string or end of line instead got redirection " \
          "'%s'\n",                                                            \
          str)
#define INVALID_SYNTAX_ERR(str)                                                \
  fprintf(stderr, "anshell: invalid syntax '%s'\n", str)
#define EXPECTED_ANDOR_OR_EOL_ERR(str)                                         \
  fprintf(stderr,                                                              \
          "anshell: expected '&' or ';' or end of line, instead got '%s'\n",   \
          str);
#define CMDLINE_READ_ERR                                                       \
  fprintf(stderr, "anshell: read error occured while reading the line\n");
#define CMDLINE_ARGSIZE_ERR(size)                                              \
  fprintf(stderr,                                                              \
          "anshell: maximum argument size limit of %ld bytes exceeded\n",      \
          size)

/// Exit codes
#define FAILURE -1
#define SUCCESS 0

#endif
