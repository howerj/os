#include "klib.h"
#include "isr.h"
#include "monitor.h"

isr_t interrupt_handlers[256];

void trap(registers_t *regs) {
        /*monitor_printf("received interrupt: %d\n", regs->interrupt_number);*/
        if(regs->interrupt_number >= IRQ_0) {
                if(regs->interrupt_number >= 40) {
                        outb(0xA0, 0x20); /*reset slave*/
			io_wait();
                }
                outb(0x20, 0x20); /*reset master*/
		io_wait();
        }
        if(interrupt_handlers[regs->interrupt_number]) {
                isr_t handler = interrupt_handlers[regs->interrupt_number];
                handler(regs);
        }
}

void register_interrupt_handler(uint8_t n, isr_t handler)
{
        interrupt_handlers[n] = handler;
}

