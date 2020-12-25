#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void *kmalloc_a(size_t sz); /**<page aligned malloc*/
void *kmalloc_p(size_t sz, uintptr_t * phy);/**<returns physical address in phy*/
void *kmalloc_ap(size_t sz, uintptr_t * phy);/**<returns pages aligned with physical address*/

extern uintptr_t placement_address;

#endif
