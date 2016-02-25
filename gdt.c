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

void PIC_remap(int master_offset, int slave_offset);
static void init_idt(void)
{
        int i;
        idt_ptr.limit = sizeof(idt_entry_struct_t) * 256-1;
        idt_ptr.base  = (uint32_t)&idt_entries;

        kmemset(&idt_entries, 0, sizeof(idt_entry_struct_t) * 256);

	PIC_remap(0x20, 0x28);

        for(i = 0; i < 256; i++)
                idt_set_gate(i, vectors[i], 0x08, 0x8E); 
	/*system call handler should get set here...*/
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

/********************************** TEST CODE ****************************/
/* From http://wiki.osdev.org/PIC */
/* reinitialize the PIC controllers, giving them specified vector offsets
   rather than 8h and 70h, as configured by default */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_READ_IRR    0x0a    /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR    0x0b    /* OCW3 irq service next CMD read */
#define ICW1_ICW4	0x01		/* ICW4 (not) needed */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04		/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x10		/* Initialization - required! */
 
#define ICW4_8086	0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C		/* Buffered mode/master */
#define ICW4_SFNM	0x10		/* Special fully nested (not) */
 
/* arguments:
	offset1 - vector offset for master PIC
		vectors on the master become offset1..offset1+7
	offset2 - same for slave PIC: offset2..offset2+7
*/
void PIC_remap(int master_offset, int slave_offset)
{
	unsigned char a1, a2;
 
	a1 = inb(PIC1_DATA); // save masks
	a2 = inb(PIC2_DATA);
 
	outb(PIC1_CMD, ICW1_INIT+ICW1_ICW4);  // starts the initialization sequence (in cascade mode)
	io_wait();
	outb(PIC2_CMD, ICW1_INIT+ICW1_ICW4);
	io_wait();
	outb(PIC1_DATA, master_offset); // ICW2: Master PIC vector offset
	io_wait();
	outb(PIC2_DATA, slave_offset); // ICW2: Slave PIC vector offset
	io_wait();
	outb(PIC1_DATA, 4); // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
	io_wait();
	outb(PIC2_DATA, 2); // ICW3: tell Slave PIC its cascade identity (0000 0010)
	io_wait();
 
	outb(PIC1_DATA, ICW4_8086);
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();
 
	outb(PIC1_DATA, a1); // restore saved masks.
	outb(PIC2_DATA, a2);
}

