#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void initialize_timer(uint32_t frequency);

/* Programmable Interrupt Timer defines
 * See http://wiki.osdev.org/Programmable_Interval_Timer */
#define PIT_CHAN0 (0x40u) /*Generate IRQ 0, r/w*/
#define PIT_CHAN1 (0x41u) /*DRAM refresh, r/w*/
#define PIT_CHAN2 (0x42u) /*PC speaker, r/w*/
#define PIT_CMD   (0x43u) /*Command, w*/
#define PIT_FREQ  (1193180u) /*The PIT runs at this frequency (Hz)*/

#endif
