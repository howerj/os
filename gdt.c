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
gdt_ptr_struct_t gdt_ptr;
idt_entry_struct_t idt_entries[256];
idt_ptr_struct_t idt_ptr;

extern uint32_t vectors[];	/* in vectors.S: array of 256 entry pointers */

void initialize_descriptor_tables(void)
{
	kprintf("(initialize 'descriptor-tables)\n");
	init_gdt();
	init_idt();
	kmemset(&interrupt_handlers, 0, sizeof(isr_t) * 256);
}

static void init_gdt(void)
{
	gdt_ptr.limit = (sizeof(gdt_entry_struct_t) * 5) - 1;
	gdt_ptr.base = (uint32_t) & gdt_entries;
	gdt_set_gate(0, 0, 0, 0, 0);
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);	/* code segment */
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);	/* data segment */
	gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);	/* user mode code segment */
	gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);	/* user mode data segment */

	gdt_flush((uint32_t) & gdt_ptr);
}

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
	gdt_entries[num].base_low = base & 0xFFFF;
	gdt_entries[num].base_middle = (base >> 16) & 0xFF;
	gdt_entries[num].base_high = (base >> 24) & 0xFF;

	gdt_entries[num].limit_low = limit & 0xFFFF;
	gdt_entries[num].granularity = (limit >> 16) & 0x0F;
	gdt_entries[num].granularity |= granularity & 0xF0;
	gdt_entries[num].access = access;
}

void PIC_remap(int master_offset, int slave_offset);
static void init_idt(void)
{
	int i;
	idt_ptr.limit = sizeof(idt_entry_struct_t) * 256 - 1;
	idt_ptr.base = (uint32_t) & idt_entries;

	kmemset(&idt_entries, 0, sizeof(idt_entry_struct_t) * 256);

	PIC_remap(0x20, 0x28);

	for (i = 0; i < 256; i++)
		idt_set_gate(i, vectors[i], 0x08, 0x8E);
	/*system call handler should get set here... */
	idt_flush((uint32_t) & idt_ptr);
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags)
{
	idt_entries[num].base_low = base & 0xFFFF;
	idt_entries[num].base_high = (base >> 16) & 0xFFFF;

	idt_entries[num].selector = selector;
	idt_entries[num].always0 = 0;
	idt_entries[num].flags = flags /* @todo "| 0x60" for user mode */ ;
}

/* From http://wiki.osdev.org/PIC */
/* reinitialize the PIC controllers, giving them specified vector offsets
   rather than 8h and 70h, as configured by default */

/* arguments:
	master_offset - vector offset for master PIC
		vectors on the master become offset1..offset1+7
	slave_offset - same for slave PIC: offset2..offset2+7
*/
void PIC_remap(int master_offset, int slave_offset)
{
	unsigned char a1, a2;

	/* save masks*/
	a1 = inb(PIC1_DATA);
	a2 = inb(PIC2_DATA);
	/* starts the initialization sequence (in cascade mode) */
	outb(PIC1_CMD, ICW1_INIT + ICW1_ICW4);
	io_wait();
	outb(PIC2_CMD, ICW1_INIT + ICW1_ICW4);
	io_wait();
	/* ICW2: Master PIC vector offset */
	outb(PIC1_DATA, master_offset);	
	io_wait();
	/* ICW2: Slave PIC vector offset */
	outb(PIC2_DATA, slave_offset);	
	io_wait();
	/* ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100) */
	outb(PIC1_DATA, 4);
	io_wait();
	/* ICW3: tell Slave PIC its cascade identity (0000 0010) */
	outb(PIC2_DATA, 2);
	io_wait();

	outb(PIC1_DATA, ICW4_8086);
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();

	/* restore saved masks */
	outb(PIC1_DATA, a1);	
	outb(PIC2_DATA, a2);
}


