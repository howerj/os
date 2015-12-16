#include "klib.h"
#include "gdt.h"

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

void init_descriptor_tables(void)
{
        init_gdt();
        init_idt();
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
        idt_ptr.limit = sizeof(idt_entry_struct_t) * 256-1;
        idt_ptr.base  = (uint32_t)&idt_entries;

        kmemset(&idt_entries, 0, sizeof(idt_entry_struct_t) * 256);

        idt_set_gate(0, (uint32_t)isr0,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr1,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr2,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr3,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr4,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr5,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr6,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr7,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr8,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr9,  0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr11, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr12, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr13, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr14, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr15, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr16, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr17, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr18, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr19, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr20, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr21, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr22, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr23, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr24, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr25, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr26, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr27, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr28, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr29, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr30, 0x08, 0x8E);
        idt_set_gate(0, (uint32_t)isr31, 0x08, 0x8E);

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

