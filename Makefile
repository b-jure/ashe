# Ashe - async linux shell
# See any of the source files in 'src' directory
# for copyright and licence details.

include config.mk

SRC = src/aalloc.c src/aashe.c src/aasync.c src/abuiltin.c src/ainput.c \
      src/ajobcntl.c src/alex.c src/aparser.c src/aprompt.c src/arun.c \
      src/ashell.c src/autils.c
OBJ = ${SRC:.c=.o}

all: options ashe

options:
	@echo ashe build options:
	@echo "CFLAGS  = ${CFLAGS}"
	@echo "LDFLAGS = ${LDFLAGS}"
	@echo "CC      = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

ashe: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f ashe ${OBJ} ashe-${VERSION}.tar.gz

dist: clean
	mkdir -p ashe-${VERSION}
	cp -r COPYING Makefile README.md config.mk ${SRC} ashe-${VERSION}
	tar -cf ashe-${VERSION}.tar ashe-${VERSION}
	gzip ashe-${VERSION}.tar
	rm -rf ashe-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ashe ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/ashe

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/ashe

.PHONY: all options clean dist install unistall
