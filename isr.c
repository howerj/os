#include "klib.h"
#include "isr.h"
#include "monitor.h"

isr_t interrupt_handlers[256];

/** @bug this should be passed by pointer! */
void isr_handler(registers_t regs)
{
        monitor_printf("received interrupt: %d\n", regs.interrupt_number);
}

void irq_handler(registers_t regs)
{
        if(regs.interrupt_number >= 40) {
                outb(0xA0, 0x20); /*reset slave*/
        }
        outb(0x20, 0x20); /*reset master*/
        if(interrupt_handlers[regs.interrupt_number]) {
                isr_t handler = interrupt_handlers[regs.interrupt_number];
                handler(regs);
        }
}

void isr_register_interrupt_handler(uint8_t n, isr_t handler)
{
        interrupt_handlers[n] = handler;
}

