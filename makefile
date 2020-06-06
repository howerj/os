CFLAGS=-Wall -Wextra -std=c99 -g -O2

all: pl0

pl0: debug.o util.o lexer.o parser.o code.o vm.o pl0.o
	@echo cc $^ -o $@
	@${CC} ${CFLAGS} $^ -o $@

test: pl0
	./$< ex1.pas ex2.pas

%.o: %.c pl0.h
	@echo cc $< -c -o $@
	@${CC} ${CFLAGS} $< -c -o $@

clean:
	rm -f pl0 *.o core vgcore.*
