#include "klib.h"
#include "gdt.h"
#include "isr.h"

extern void gdt_flush(uint32_t);
extern void idt_flush(uint32_t);

static void init_gdt(void);
static void gdt_set_gate(int32_t, uint32_t, uint32_t, uint8_t, uint8_t);

static void init_idt(void);
static void idt_set_gate(uint8_t, uint32_t, uint16_t, uint8_t);

gdt_entry_struct_t gdt_entries[5];
gdt_ptr_struct_t   gdt_ptr;
idt_entry_struct_t idt_entries[256];
idt_ptr_struct_t   idt_ptr;

extern uint32_t vectors[];  /* in vectors.S: array of 256 entry pointers*/

void init_descriptor_tables(void)
{
        init_gdt();
        init_idt();
        kmemset(&interrupt_handlers, 0, sizeof(isr_t)*256);
}

static void init_gdt(void)
{
        gdt_ptr.limit = (sizeof(gdt_entry_struct_t)*5) - 1;
        gdt_ptr.base  = (uint32_t) &gdt_entries;
        gdt_set_gate(0, 0, 0, 0, 0);
        gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* code segment */
        gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* data segment */
        gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* user mode code segment */
        gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* user mode data segment */

        gdt_flush((uint32_t)&gdt_ptr);
}

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
        gdt_entries[num].base_low    =  base & 0xFFFF;
        gdt_entries[num].base_middle = (base >> 16) & 0xFF;
        gdt_entries[num].base_high   = (base >> 24) & 0xFF;

        gdt_entries[num].limit_low   =  limit & 0xFFFF;
        gdt_entries[num].granularity =  (limit >> 16) & 0x0F;
        gdt_entries[num].granularity |= granularity & 0xF0;
        gdt_entries[num].access      =  access;
}


static void init_idt(void)
{
        int i;
        idt_ptr.limit = sizeof(idt_entry_struct_t) * 256-1;
        idt_ptr.base  = (uint32_t)&idt_entries;

        kmemset(&idt_entries, 0, sizeof(idt_entry_struct_t) * 256);

        /* Remap the irq table */
        outb(0x20, 0x11);
        outb(0xA0, 0x11);
        outb(0x21, 0x20);
        outb(0xA1, 0x28);
        outb(0x21, 0x04);
        outb(0xA1, 0x02);
        outb(0x21, 0x01);
        outb(0xA1, 0x01);
        outb(0x21, 0x0);
        outb(0xA1, 0x0);

        for(i = 0; i < 256; i++)
                idt_set_gate(i, vectors[i], 0x08, 0x8E); 
        idt_flush((uint32_t)&idt_ptr);
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags)
{
        idt_entries[num].base_low  = base & 0xFFFF;
        idt_entries[num].base_high = (base >> 16) & 0xFFFF;

        idt_entries[num].selector = selector;
        idt_entries[num].always0  = 0;
        idt_entries[num].flags    = flags /* @todo "| 0x60" for user mode */;
}

