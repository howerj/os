# flush.s -- contains global descriptor table and interrupt descriptor table
#          setup code.
#          Based on code from Bran's kernel development tutorials.
#          Rewritten for JamesM's kernel development tutorials.
#          ..and rewritten again for my Kernel - RJH

.global gdt_flush

gdt_flush:
	mov 4(%esp), %eax	# get new GDT pointer passed as a parameter 
	lgdt (%eax)		# load new GDT pointer
	
	mov $0x10, %ax		# 0x10 is the offset in the GDT to our data segment
	mov %ax, %ds		# load all data segment selectors
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	jmp $0x08, $flush	# 0x08 is the offset to our code segment, ljmp long/far jump
flush:
	ret

.global idt_flush

idt_flush:
	mov 4(%esp), %eax	# get pointer to IDT passed as param
	lidt (%eax)		# load IDT pointer
	ret
