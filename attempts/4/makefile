FAT32_VERSION=0.0.1
STD=c99
CFLAGS=-Wall -Wextra -fPIC -std=${STD} -Os -g -pedantic -fwrapv ${DEFINES} ${EXTRA} -DVERSION="\"${FAT32_VERSION}\""
TARGET=fat32
AR      = ar
ARFLAGS = rcs
DESTDIR = install

ifeq ($(OS),Windows_NT)
EXE=.exe
DLL=dll
DLLIBS=
else # Assume Unixen
EXE=
DLL=so
DLLIBS=
endif

.PHONY: all test clean dist install

all: ${TARGET}

test: ${TARGET}
	./${TARGET} -t

main.o: main.c ${TARGET}.h

${TARGET}.o: ${TARGET}.c ${TARGET}.h

win.o:  win.c ${TARGET}.h

unix.o: STD=gnu99
unix.o: unix.c ${TARGET}.h

lib${TARGET}.a: ${TARGET}.o ${TARGET}.h
	${AR} ${ARFLAGS} $@ ${TARGET}.o 

lib${TARGET}.${DLL}: ${TARGET}.o ${TARGET}.h
	${CC} ${CFLAGS} -shared ${TARGET}.o ${DLLIBS} -o $@

${TARGET}: main.o lib${TARGET}.a
	${CC} ${CFLAGS} $^ ${LDLIBS} -o $@
	-strip $@${EXE}

${TARGET}.1: readme.md
	-pandoc -s -f markdown -t man $< -o $@

install: ${TARGET} lib${TARGET}.a lib${TARGET}.${DLL} ${TARGET}.1 .git
	install -p -D ${TARGET} ${DESTDIR}/bin/${TARGET}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -m 755 -D lib${TARGET}.${DLL} ${DESTDIR}/lib/lib${TARGET}.${DLL}
	install -p -m 644 -D ${TARGET}.h ${DESTDIR}/include/${TARGET}.h
	-install -p -m 644 -D ${TARGET}.1 ${DESTDIR}/man/${TARGET}.1
	mkdir -p ${DESTDIR}/src
	cp -a .git ${DESTDIR}/src
	cd ${DESTDIR}/src && git reset --hard HEAD

dist: install
	tar zcf ${TARGET}-${VERSION}.tgz ${DESTDIR}

clean:
	git clean -dffx

