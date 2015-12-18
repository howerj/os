#include "timer.h"
#include "isr.h"
#include "monitor.h"
#include "klib.h"

static uint32_t ticks = 0;

static void timer_callback(registers_t *regs)
{
        ticks++;
        monitor_printf("Tick: %u\n", ticks);
}

void init_timer(uint32_t frequency)
{
        register_interrupt_handler(IRQ_0, &timer_callback);

        /* The PIT (programmable interrupt timer) runs at 1193180 Hz */
        uint32_t divisor = 1193180 / frequency;

        /* command */
        outb(0x43, 0x36);

        uint8_t l = (uint8_t)(divisor & 0xFF);
        uint8_t h = (uint8_t)((divisor >> 8u) & 0xFF);

        /* send the divisor */
        outb(0x40, l);
        outb(0x40, h);
}

