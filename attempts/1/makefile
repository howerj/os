CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -pedantic -g
TARGET=vm

.PHONY: all run clean

all: ${TARGET} pas hexy

run: ${TARGET} disk.img
	./${TARGET} disk.img

%.img: %.hex hexy
	./hexy $< $@

%.hex: %.pas pas
	./pas $< $@

pas: pas.c

clean:
	rm -fv ${TARGET} hexy pas *.o *.a *.exe
