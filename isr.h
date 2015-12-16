#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef struct {
        uint32_t ds;
        uint32_t edi, esi, ebp, esp_useless, ebx, edx, ecx, eax; /* pushed by pusha */
        uint32_t interrupt_number, error_code; /* if applicable */
        uint32_t eip, cs, eflags, esp, ss; /* pushed by the processor automatically */
} registers_t;

#endif
