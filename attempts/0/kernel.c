/*Originally from: <http://wiki.osdev.org/Bare_Bones>*/
#include "klib.h"
#include "timer.h"
#include "gdt.h"
#include "kheap.h"
#include "paging.h"
#include "kbd.h"

/* This tutorial will only work for the 32-bit ix86 targets. Cross compiled */
#if !defined(__i386__) || defined(__linux__)
#error "This kernel needs to be cross compiled compiled with a ix86-elf compiler"
#endif

typedef struct {
	uint32_t magic;
	uint32_t flags;
	uint32_t checksum;
	uint32_t *mboot, *code, *bss, *end;
} multiboot_header;

typedef struct {
	uint32_t *code;
	uint32_t *bss;
	uint32_t *stack;
	uint32_t *end;
	int argc;
	char **argv;
} kparam;

void kernel_main(multiboot_header * bh)
{
	disable();
	kprintf("%C%Fb%Br(kernel 0.01 'start)%D\n");

	if (bh)
		kprintf("(boot-header 'magic %x 'flags %x 'cksum %x 'mboot %x\n\t'code %x 'bss %x 'end %x)\n",
			bh->magic, bh->flags, bh->checksum, bh->mboot, bh->code, bh->bss, bh->end);
	else
		kprintf("(boot-header nil)");

	initialize_descriptor_tables();
	initialize_interrupt_handlers();
	initialize_keyboard();
	initialize_timer(100);
	initialize_paging();
	enable();
	/*asm volatile("int $0x3"); */
	uint32_t *ptr = (uint32_t *) 0xA0000000;
	uint32_t do_page_fault = *ptr;
	kprintf("a %x\n", do_page_fault);
}

