#include "klib.h"
#include "kheap.h"

extern uintptr_t end;		/*defined in linker script */
uintptr_t placement_address = (uintptr_t) & end;

static void *kmalloc_internal(size_t sz, int align, uintptr_t * phy)
{
	uintptr_t tmp;
	if (align && (placement_address & 0xFFFFF000)) {	/*if the address if not already page aligned */
		placement_address &= 0xFFFFF000;
		placement_address += 0x1000;
	}
	placement_address = (placement_address + (0x8 - 0x1)) & -8;	/*round up to 8-byte boundary */
	placement_address &= -4;	/*round down to 4-byte boundary */
	tmp = placement_address;
	if (phy)
		*phy = placement_address;
	placement_address += sz;
	return (void*)tmp;
}

void *kmalloc(size_t sz)
{
	return kmalloc_internal(sz, 0, NULL);
}

void *kmalloc_a(size_t sz)
{
	return kmalloc_internal(sz, 1, NULL);
}

void *kmalloc_p(size_t sz, uintptr_t * phy)
{
	return kmalloc_internal(sz, 0, phy);
}

void *kmalloc_ap(size_t sz, uintptr_t * phy)
{
	return kmalloc_internal(sz, 1, phy);
}

