#include "timer.h"
#include "isr.h"
#include "klib.h"

static uint32_t ticks = 0;

static void timer_callback(registers_t * regs)
{
	ticks++;
	kprintf("(tick %u)\n", ticks);
}

void initialize_timer(uint32_t frequency)
{
	kprintf("(initialize 'timer %u)\n", frequency);
	register_interrupt_handler(IRQ_0, &timer_callback);

	uint32_t divisor = PIT_FREQ / frequency;

	/* command 
	 * 0x36
	 *   ||
	 *   |.--> 0x3 << 1 = Generate repeating square wave
	 *   .---> 0x3 << 4 = Send lo-byte then hi-byte of divisor
	 * Bits 0, 6 and 7 are not used.
	 */
	outb(PIT_CMD, 0x36);

	uint8_t l = (uint8_t) (divisor & 0xFF);
	uint8_t h = (uint8_t) ((divisor >> 8u) & 0xFF);

	/* send the divisor */
	outb(PIT_CHAN0, l);
	outb(PIT_CHAN0, h);
}
