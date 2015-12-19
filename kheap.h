#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

uint32_t kmalloc(size_t sz);
uint32_t kmalloc_a(size_t sz); /**<page aligned malloc*/
uint32_t kmalloc_p(size_t sz, uint32_t *phy); /**<returns physical address in phy*/
uint32_t kmalloc_ap(size_t sz, uint32_t *phy); /**<returns pages aligned with physical address*/

extern uint32_t placement_address;

#endif
