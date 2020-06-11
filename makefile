CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g
TARGET=vm

.PHONY: all run clean

all: ${TARGET} pas asm hexy

run: ${TARGET} disk.img
	./${TARGET} disk.img

%.img: %.hex hexy
	./hexy $< $@

%.hex: %.asm asm
	./asm $< $@

%.hex: %.pas pas
	./pas $< $@

pas: pas.c

asm: asm.c

clean:
	rm -fv ${TARGET} asm hexy pas *.o *.a *.exe
