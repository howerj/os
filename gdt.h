#ifndef GDT_H
#define GDT_H

#include <stdint.h>

typedef struct __attribute__ ((packed)) {
	uint16_t limit_low;	/* lower 16-bits of base */
	uint16_t base_low;	/* lower 16-bits of base */
	uint8_t base_middle;	/* next 8 bits of base */
	uint8_t access;		/* access flags, determines which ring segment can be used in */
	uint8_t granularity;	/* 7 | 6 | 5 | 4 |    3 0
				   G | D | 0 | A | Seg-Length */
	uint8_t base_high;	/* last 8 bits of base */
} gdt_entry_struct_t;

typedef struct __attribute__ ((packed)) {
	uint16_t limit;		/* upper 16-bits of all selectors */
	uint32_t base;		/* address of first gdt_entry_structure */
} gdt_ptr_struct_t;

typedef struct __attribute__ ((packed)) {
	uint16_t base_low;	/* lower 16-bits of an address to jump to when an interrupt fires */
	uint16_t selector;	/* the kernel segment selector */
	uint8_t always0;	/* must be zero */
	uint8_t flags;		/* 7 | 6  5 | 4  0 
				   P |  DPL | Always 00110 
				   DPL = Descriptor Privilege Level, 0 = kernel, 3 = user
				   P = Type, Code or data */
	uint16_t base_high;	/* upper 16-bits of address to jump to */
} idt_entry_struct_t;

typedef struct __attribute__ ((packed)) {
	uint16_t limit;
	uint32_t base;		/* the address of the first element in our idt_entry_struct_t array */
} idt_ptr_struct_t;

void initialize_descriptor_tables(void);

/* PIC Commands */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_READ_IRR    0x0a	/* OCW3 irq ready next CMD read */
#define PIC_READ_ISR    0x0b	/* OCW3 irq service next CMD read */
#define ICW1_ICW4	0x01	/* ICW4 (not) needed */
#define ICW1_SINGLE	0x02	/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04	/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08	/* Level triggered (edge) mode */
#define ICW1_INIT	0x10	/* Initialization - required! */

#define ICW4_8086	0x01	/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02	/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08	/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C	/* Buffered mode/master */
#define ICW4_SFNM	0x10	/* Special fully nested (not) */

#endif
