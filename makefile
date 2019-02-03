CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -O2
TARGET=vm

.PHONY: run clean

run: ${TARGET}
	./${TARGET}

clean:
	rm -fv ${TARGET} *.o *.a *.exe
