# ashe version
VERSION = 1.0.0


# Install prefix
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man


# AddresSanitizer
#ASANFLAGS = -fsanitize=address -fsanitize=undefined


# Debug definitions
#DBGDEFS = -DASHE_DBG -DASHE_DBG_ASSERT -DASHE_DBG_LEX -DASHE_DBG_LINES \
	  -DASHE_DBG_CURSOR -DASHE_DBG_LEX -DASHE_DBG_MAIN -DASHE_DBG_AST


# Debug flags
#DBGFLAGS = -g


# Shared libraries
LIBS = ${ASANFLAGS}


# Optimization flags
OPTS = -O2


# compiler and linker flags
CPPFLAGS = -D_POSIX_SOURCE_200809L -D_POSIX_C_SOURCE -D_DEFAULT_SOURCE ${DBGDEFS}
CFLAGS = -std=c99 -Wpedantic -Wall -Wextra ${OPTS} ${DBGFLAGS} ${ASANFLAGS} ${CPPFLAGS}
LDFLAGS = ${LIBS}


# compiler and linker
CC = gcc
