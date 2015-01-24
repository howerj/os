GCC=i686-elf-gcc
AS=i686-elf-as
QEMU=qemu-system-i386
CC=i686-elf-gcc
CFLAGS=-std=gnu99 -ffreestanding -O2 -Wall -Wextra


.PHONY: all clean

all: myos.bin myos.iso

boot.o: boot.s
	$(AS) $< -o $@

kstring.o: kstring.c kstring.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel.o: kernel.c 
	$(CC) $(CFLAGS) -c $< -o $@

myos.bin: kernel.o boot.o kstring.o linker.ld
	$(CC) -T linker.ld -o myos.bin -ffreestanding -O2 -nostdlib boot.o kernel.o kstring.o -lgcc

myos.iso: myos.bin
	cp myos.bin isodir/boot/myos.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

#incase grub-mkrescue did not work
#run: myos.bin
#	$(QEMU) -kernel $<

run: myos.iso
	$(QEMU) -cdrom $<

clean:
	rm -f *.o *.bin *.iso isodir/boot/*.bin isodir/boot/grub/*.cfg
