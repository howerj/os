#include "klib.h"
#include "isr.h"

isr_t interrupt_handlers[256];

void trap(registers_t * regs)
{
	/*kprintf("received interrupt: %d\n", regs->interrupt_number); */
	if (regs->interrupt_number >= IRQ_0) {
		if (regs->interrupt_number >= 40) {
			outb(0xA0, 0x20);	/*reset slave */
			io_wait();
		}
		outb(0x20, 0x20);	/*reset master */
		io_wait();
	}
	if (interrupt_handlers[regs->interrupt_number]) {
		isr_t handler = interrupt_handlers[regs->interrupt_number];
		handler(regs);
	}
}

void register_interrupt_handler(uint8_t n, isr_t handler)
{
	interrupt_handlers[n] = handler;
}

void initialize_interrupt_handlers(void)
{
	kprintf("(initialize 'interrupt-handlers)\n");
	for (int i = 0; i <= 0x1f; i++)
		register_interrupt_handler(i, print_registers);
}

void print_registers(registers_t * regs)
{
	kprintf("(registers\n");
	kprintf("\tedi %x\n\tesi %x\n\tebp %x\n\toesp %x\n\tebx %x\n\tedx %x\n\tecx %x\n\teax %x\n",
		regs->edi, regs->esi, regs->ebp, regs->oesp, regs->ebx, regs->edx, regs->ecx, regs->eax);
	kprintf("\tgs %x\n\tfs %x\n\tes %x\n\tds %x\n\ttrapno %x\n\terror %x\n\teip %x\n\tcs %x\n\teflags %x\n\tesp %x\n\tss %x",
		regs->gs, regs->fs, regs->es, regs->ds, regs->interrupt_number, regs->error_code,
		regs->eip, regs->cs, regs->eflags, regs->esp, regs->ss);
	kprintf(")\n");
}


