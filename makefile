CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g
TARGET=vm

.PHONY: run clean

all: ${TARGET}

run: ${TARGET}
	./${TARGET} disk.img

%.img: %.hex hexy

clean:
	rm -fv ${TARGET} pas *.o *.a *.exe
