.text
/* Multiboot header */
.section .multiboot
.align 4
.long 0x1BADB002               /*magic header number*/
.long 0x00000003               /*flags; 0x1 == align modules on page boundaries, 0x2 == provide memory map */
.long -(0x1BADB002+0x00000003) /*= -(flags|magic) check sum*/
 
/*.section .bootstrap_stack, "aw", @nobits*/
.section .bootstrap_stack
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

.text
/* Kernel entry point. */
        .globl _start
_start:
	movl    $stack_top, %esp /*setup stack*/
        call    kmain
        cli
        hlt
hang:
        jmp     hang

.size _start, . - _start
