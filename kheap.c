#include "klib.h"
#include "kheap.h"

extern uint32_t end; /*defined in linker script*/
uint32_t placement_address = (uint32_t)&end;

static uint32_t kmalloc_internal(size_t sz, int align, uint32_t *phy)
{
        uint32_t tmp;
        if(align && (placement_address & 0xFFFFF000)) { /*if the address if not already page aligned*/
                placement_address &= 0xFFFFF000;
                placement_address += 0x1000;
        }
        placement_address = (placement_address + (0x8 - 0x1)) & -8; /*round up to 8-byte boundary*/
        placement_address &= -4; /*round down to 4-byte boundary*/
        tmp = placement_address;
        placement_address += sz;
        if(phy)
                *phy = placement_address;
        return tmp;
}

uint32_t kmalloc(size_t sz)
{
        return kmalloc_internal(sz, 0, NULL);
}

uint32_t kmalloc_a(size_t sz)
{
        return kmalloc_internal(sz, 1, NULL);
}

uint32_t kmalloc_p(size_t sz, uint32_t *phy)
{
        return kmalloc_internal(sz, 0, phy);
}

uint32_t kmalloc_ap(size_t sz, uint32_t *phy)
{
        return kmalloc_internal(sz, 1, phy);
}

