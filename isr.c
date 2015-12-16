#include "klib.h"
#include "isr.h"
#include "monitor.h"

void isr_handler(registers_t *regs)
{
        monitor_puts("received interrupt: \n");
}

