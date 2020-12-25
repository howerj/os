#include "kbd.h"
#include "klib.h"
#include "isr.h"

/* See http://wiki.osdev.org/PS/2_Keyboard */
int initialize_keyboard(void)
{
	kprintf("(initialize 'keyboard)\n");
	register_interrupt_handler(0x20 + 1, print_registers);
	/* TODO */
	return 0;
}

