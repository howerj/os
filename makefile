GCC=i686-elf-gcc
AS=i686-elf-as
QEMU=qemu-system-i386
CC=i686-elf-gcc
CFLAGS=-std=gnu99 -ffreestanding -O2 -Wall -Wextra

.PHONY: all clean

all: kernel.bin

boot.o: boot.s
	$(AS) $< -o $@

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	nasm -felf $< -o $@

OBJFILES=kernel.o boot.o gdt.o klib.o monitor.o gdt_flush.o interrupt.o isr.o

kernel.bin:  $(OBJFILES) linker.ld
	$(CC) -T linker.ld -o $@ -ffreestanding -O2 -nostdlib $(OBJFILES) -lgcc

#incase grub-mkrescue did not work
#run: kernel.bin
#	$(QEMU) -kernel $<

# -boot d == boot from cdrom, for quicker booting
#run: kernel.iso
#	$(QEMU) -cdrom $< -boot d

floppy.img: kernel.bin
	sudo losetup /dev/loop0 floppy.img
	sudo mount /dev/loop0 /mnt
	sudo cp $< /mnt/kernel
	sudo umount /dev/loop0
	sudo losetup -d /dev/loop0 

run: floppy.img
	$(QEMU) -fda $< -boot d
	#$(QEMU) -kernel $< -boot d

clean:
	rm -f *.o *.bin *.iso 
