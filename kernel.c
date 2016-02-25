/*Originally from: <http://wiki.osdev.org/Bare_Bones>*/
#include "klib.h"
#include "timer.h"
#include "gdt.h"
#include "monitor.h"
#include "paging.h"
#include "kheap.h"
 
/* This tutorial will only work for the 32-bit ix86 targets. Cross compiled */
#if !defined(__i386__) || defined(__linux__)
#error "This kernel needs to be cross compiled compiled with a ix86-elf compiler"
#endif
 
static void print_callback(registers_t *regs) {
	monitor_printf("unhandled exception.\n");
	monitor_printf("edi %x\tesi %x\tebp %x\toesp %x\nebx %x\tedx %x\tecx %x\teax %x\n",
		regs->edi, regs->esi, regs->ebp, regs->oesp, regs->ebx, regs->edx, regs->ecx, regs->eax);
	monitor_printf("gs %x, fs %x, es %x, ds %x, trapno %x\n error %x, eip %x, cs %x, eflags %x, esp %x, ss %x\n",
		regs->gs, regs->fs, regs->es, regs->ds, regs->interrupt_number, regs->error_code,
		regs->eip, regs->cs, regs->eflags, regs->esp, regs->ss);
}

void kernel_main(uint32_t p)
{
        asm volatile("cli");
        monitor_clear();
        monitor_set_background_color(COLOR_RED);
        monitor_set_foreground_color(COLOR_BLUE);
        monitor_printf("Kernel v0.01a Start\n");
        monitor_printf("value: %d\nplacement %x\n", p, placement_address);
        init_descriptor_tables();
        monitor_default_colors();
	//init_timer(50);
	for(int i = 0; i <= 0x1f; i++)
		register_interrupt_handler(i, print_callback);
	register_interrupt_handler(0x20+1, print_callback);
        initialize_paging();
	monitor_printf("placement: %x\n", placement_address);
        //asm volatile("int $0x3");
        asm volatile("sti");
    	uint32_t *ptr = (uint32_t*)0xA0000000;
    	uint32_t do_page_fault = *ptr;
	monitor_printf("lol: %x\n", do_page_fault);

        //asm volatile("int $0x4");
}

