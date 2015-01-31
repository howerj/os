.text
/* Multiboot header */
.align 4
.long 0x1BADB002               /*magic header number*/
.long 0x00000003               /*flags; 0x1 == align modules on page boundaries, 0x2 == provide memory map */
.long -(0x1BADB002+0x00000003) /*= -(flags|magic) check sum*/
 
/* Kernel entry point. */
        .globl _start
_start:
        /* Call the main kernel function. */
        call    kmain
loop:
        jmp     loop
