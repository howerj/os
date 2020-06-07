CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g
TARGET=vm

.PHONY: run clean

all: ${TARGET}

run: ${TARGET} disk.img
	./${TARGET} disk.img

%.img: %.hex hexy
	./hexy $< $@

clean:
	rm -fv ${TARGET} pas *.o *.a *.exe
