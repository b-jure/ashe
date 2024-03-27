# ashe version
VERSION = 1.0.0

PREFIX = /usr/local

# Uncomment and use these flags to create a debug build You do require google's
# AddressSanitizer though AddressSanitizer
ASANLIB = /usr/lib
ASANFLAGS = -ggdb -fsanitize=address -fsanitize=undefined
ASANLDFLAGS = -fsanitize=address -fsanitize=undefined

LIBS = -L${ASANLIB} ${ASANLDFLAGS}

CPPFLAGS = -D_POSIX_SOURCE_200809L -D_POSIX_C_SOURCE -D_DEFAULT_SOURCE \
	   -DASHE_DBG -DASHE_DBG_LINES -DASHE_DBG_CURSOR
CFLAGS = -std=c99 -Wpedantic -Wall -Wextra ${CPPFLAGS} ${ASANFLAGS}
LDFLAGS = ${LIBS}

CC = cc
