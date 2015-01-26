
start32.o: start32.asm
	fasm $<

kernel.o: kernel.c
	tcc -c kernel.c

kernel-i386.elf:
	tcc -nostdlib -Wl,-Ttext,0x100000 start32.o kernel.o -o kernel-i386.elf

kernel-i386.bin:
	tcc -nostdlib -Wl,-Ttext,0x100000 -Wl,--oformat,binary -static start32.o kernel.o -o kernel-i386.bin

clean:
	rm *.o *.bin *.elf
