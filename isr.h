#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef enum {
	IRQ_0 = 32,
	IRQ_1 = 33,
	IRQ_2 = 34,
	IRQ_3 = 35,
	IRQ_4 = 36,
	IRQ_5 = 37,
	IRQ_6 = 38,
	IRQ_7 = 39,
	IRQ_8 = 40,
	IRQ_9 = 41,
	IRQ_10 = 42,
	IRQ_11 = 43,
	IRQ_12 = 44,
	IRQ_13 = 45,
	IRQ_14 = 46,
	IRQ_15 = 47,
} irqs;

typedef struct {
	/* registers as pushed by pusha */
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t oesp;		/* useless & ignored */
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;

	/* rest of trap frame */
	uint16_t gs;
	uint16_t padding1;
	uint16_t fs;
	uint16_t padding2;
	uint16_t es;
	uint16_t padding3;
	uint16_t ds;
	uint16_t padding4;
	uint32_t interrupt_number;

	/* below here defined by x86 hardware */
	uint32_t error_code;
	uint32_t eip;
	uint16_t cs;
	uint16_t padding5;
	uint32_t eflags;

	/* below here only when crossing rings, such as from user to kernel */
	uint32_t esp;
	uint16_t ss;
	uint16_t padding6;
} registers_t;

typedef void (*isr_t) (registers_t *);
void register_interrupt_handler(uint8_t n, isr_t handler);
void print_registers(registers_t * regs);
void initialize_interrupt_handlers(void);

extern isr_t interrupt_handlers[256];

#endif
