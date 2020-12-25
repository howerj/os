GCC=i686-elf-gcc
AS=i686-elf-as
QEMU=qemu-system-i386
CC=i686-elf-gcc
CFLAGS=-std=gnu99 -ffreestanding -O2 -Wall -Wextra -g

.PHONY: all clean

all: kernel.bin

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) $^ -o $@

vectors.s: vectors.pl
	./$^ > $@

OBJFILES=kernel.o boot.o gdt.o klib.o vga.o flush.o isr.o \
	 timer.o vectors.o trapasm.o kheap.o paging.o kbd.o

kernel.bin: $(OBJFILES) linker.ld
	$(CC) -T linker.ld -o $@ -ffreestanding -nostdlib $(OBJFILES) -lgcc

#incase grub-mkrescue did not work
#run: kernel.bin
#	$(QEMU) -kernel $<

floppy.img: kernel.bin
	cp floppy/floppy.img .
	sudo losetup /dev/loop0 floppy.img
	sudo mount /dev/loop0 /mnt
	sudo cp $< /mnt/kernel
	sudo umount /dev/loop0
	sudo losetup -d /dev/loop0 

run: floppy.img
	#$(QEMU) -fda $< -boot d
	$(QEMU) -s -m 32 -kernel kernel.bin 

debug: floppy.img
	-$(QEMU) -S -s -m 32 -kernel kernel.bin &
	-sleep 1
	gdb -x startup.gdb
clean:
	rm -f *.o *.bin *.iso 
