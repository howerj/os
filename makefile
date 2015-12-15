GCC=i686-elf-gcc
AS=i686-elf-as
QEMU=qemu-system-i386
CC=i686-elf-gcc
CFLAGS=-std=gnu99 -ffreestanding -O2 -Wall -Wextra

.PHONY: all clean

all: myos.bin

boot.o: boot.s
	$(AS) $< -o $@

klib.o: klib.c klib.h
	$(CC) $(CFLAGS) -c $< -o $@

monitor.o: monitor.c monitor.h klib.h 
	$(CC) $(CFLAGS) -c $< -o $@

kernel.o: kernel.c klib.h
	$(CC) $(CFLAGS) -c $< -o $@

myos.bin: kernel.o boot.o klib.o monitor.o linker.ld
	$(CC) -T linker.ld -o myos.bin -ffreestanding -O2 -nostdlib boot.o kernel.o klib.o monitor.o -lgcc

myos.iso: myos.bin grub.cfg
	cp myos.bin isodir/boot/myos.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

#incase grub-mkrescue did not work
#run: myos.bin
#	$(QEMU) -kernel $<

# -boot d == boot from cdrom, for quicker booting
#run: myos.iso
#	$(QEMU) -cdrom $< -boot d

run: myos.bin
	$(QEMU) -kernel $< -boot d

clean:
	rm -f *.o *.bin *.iso isodir/boot/*.bin isodir/boot/grub/*.cfg
