GCC=i686-elf-gcc
AS=i686-elf-as
QEMU=qemu-system-i386

.PHONY: all clean

all: myos.bin myos.iso

boot.o: boot.s
	$(AS) $< -o $@

kernel.o: kernel.c
	i686-elf-gcc -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra

myos.bin: kernel.o boot.o linker.ld
	i686-elf-gcc -T linker.ld -o myos.bin -ffreestanding -O2 -nostdlib boot.o kernel.o -lgcc

myos.iso: myos.bin
	cp myos.bin isodir/boot/myos.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

#run: myos.bin
#	$(QEMU) -kernel $<

clean:
	rm -f *.o *.bin *.iso isodir/boot/*.bin isodir/boot/grub/*.cfg
