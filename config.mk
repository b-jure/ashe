# ashe version
VERSION = 1.0.0

# Install prefix
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/man

# AddresSanitizer
ASANLIB = /usr/lib
ASANFLAGS = -fsanitize=address -fsanitize=undefined

# Shared libraries
LIBS = -L${ASANLIB}

# Debug definitions (builtin)
DBGDEFS = -DASHE_DBG -DASHE_DBG_LINES -DASHE_DBG_CURSOR -DASHE_DBG_ASSERT -DASHE_DBG_LEX

# Debug flags
DBGFLAGS = -g

CPPFLAGS = -D_POSIX_SOURCE_200809L -D_POSIX_C_SOURCE -D_DEFAULT_SOURCE ${DBGDEFS}
CFLAGS = -std=c99 -Wpedantic -Wall -Wextra ${DBGFLAGS} ${ASANFLAGS} ${CPPFLAGS}
LDFLAGS = ${LIBS} ${ASANFLAGS}

CC = cc
