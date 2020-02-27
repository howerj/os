CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -O2
TARGET=vm

.PHONY: run clean

%.out: %.pas pas
	./pas %.pas %.out

run: ${TARGET}
	./${TARGET}

clean:
	rm -fv ${TARGET} pas *.o *.a *.exe
