#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef enum {
        IRQ_0  = 32,
        IRQ_1  = 33,
        IRQ_2  = 34,
        IRQ_3  = 35,
        IRQ_4  = 36,
        IRQ_5  = 37,
        IRQ_6  = 38,
        IRQ_7  = 39,
        IRQ_8  = 40,
        IRQ_9  = 41,
        IRQ_10 = 42,
        IRQ_11 = 43,
        IRQ_12 = 44,
        IRQ_13 = 45,
        IRQ_14 = 46,
        IRQ_15 = 47,
} irqs;

typedef struct {
        uint32_t ds;
        uint32_t edi, esi, ebp, esp_useless, ebx, edx, ecx, eax; /* pushed by pusha */
        uint32_t interrupt_number, error_code; /* if applicable */
        uint32_t eip, cs, eflags, esp, ss; /* pushed by the processor automatically */
} registers_t;

typedef void (*isr_t)(registers_t*);
void isr_register_interrupt_handler(uint8_t n, isr_t handler);

#endif
