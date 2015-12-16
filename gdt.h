#ifndef GDT_H
#define GDT_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
        uint16_t limit_low;   /* lower 16-bits of base */
        uint16_t base_low;    /* lower 16-bits of base */
        uint8_t  base_middle; /* next 8 bits of base */
        uint8_t  access;      /* access flags, determines which ring segment can be used in */
        uint8_t  granularity; /* 7 | 6 | 5 | 4 |    3 0
                                 G | D | 0 | A | Seg-Length*/
        uint8_t  base_high;   /* last 8 bits of base */
} gdt_entry_struct_t;

typedef struct __attribute__((packed)) {
        uint16_t limit; /* upper 16-bits of all selectors */
        uint32_t base;  /* address of first gdt_entry_structure */
} gdt_ptr_struct_t;

typedef struct __attribute__((packed)) {
        uint16_t base_low; /* lower 16-bits of an address to jump to when an interrupt fires */
        uint16_t selector; /* the kernel segment selector */
        uint8_t  always0;  /* must be zero */
        uint8_t  flags;    /* 7 | 6  5 | 4  0 
                              P |  DPL | Always 00110 
                             DPL = Descriptor Privilege Level, 0 = kernel, 3 = user
                             P = Type, Code or data */
        uint16_t base_high;/* upper 16-bits of address to jump to*/
} idt_entry_struct_t;

typedef struct __attribute__((packed)) {
        uint16_t limit;
        uint32_t base; /* the address of the first element in our idt_entry_struct_t array*/
} idt_ptr_struct_t;

void gdt_init_descriptor_tables(void);

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);


#endif
