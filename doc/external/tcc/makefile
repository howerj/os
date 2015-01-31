CC=tcc
AS=tcc
CFLAGS=-Wall
# You may have to change this
QEMU=qemu-system-i386
TARGET=kernel-i386

all: kernel-i386.bin kernel-i386.elf

start32.o: start32.S
	$(AS) -c start32.S

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c 

$(TARGET).bin: start32.o kernel.o
	$(CC) $(CFLAGS) -nostdlib -Wl,-Ttext,0x100000 -Wl,--oformat,binary -static $^ -o $@

$(TARGET).elf: start32.o kernel.o
	$(CC) $(CFLAGS) -nostdlib -Wl,-Ttext,0x100000 $^ -o $@

run: $(TARGET).elf
	$(QEMU) -kernel $(TARGET).elf

clean:
	rm -f *.o *.bin *.elf
