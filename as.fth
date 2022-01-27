\ Richard James Howe, howe.r.j.89@gmail.com, 
\ Temporary Assembler, Public Domain
only forth definitions hex
1 20 lshift 2* 0= [if] .( 64-bit Forth needed ) cr abort [then]
1 cells 8 <> [if] .( 64-bit Forth needed ) cr abort [then]

: (order) ( w wid*n n -- wid*n w n )
  dup if
    1- swap >r recurse over r@ xor
    if 1+ r> -rot exit then rdrop
  then ;
: -order get-order (order) nip set-order ;
: +order dup >r -order get-order r> swap 1+ set-order ;

wordlist constant meta.1
wordlist constant target.1
wordlist constant assembler.1
wordlist constant target.only.1

meta.1 +order definitions

   8 constant =cell
4000 constant size

create tflash tflash size cells allot size erase
variable tdp 0 tdp !
variable tlast 0 tlast !
variable tlocal 0 tlocal !
variable tep size =cell - tep !
000a constant =lf

: :m meta.1 +order definitions : ;
: ;m postpone ; ; immediate
:m .end only forth definitions decimal ;m
:m tcell =cell ;m
:m tcells tcell * ;m
:m there tdp @ ;m
:m tc! tflash + c! ;m
:m tc@ tflash + c@ ;m
:m t! tflash + ! ;m
:m t@ tflash + @ ;m
:m hex# ( u -- addr len ) 
   0 <# base @ >r hex =lf hold #S r> base ! #> ;m
:m save-hex ( <name> -- )
  parse-word w/o create-file throw
  there 0 ?do 
    i t@  over >r hex# r> write-file throw tcell 
  +loop
   close-file throw ;m
:m t, there t! =cell tdp +! ;m

0000000080000000 constant MEMORY_START
0000000004000000 constant IO_START
0000000008000000 constant IO_END
  40 constant TLB_ENTRIES
  20 constant TRAPS
2000 constant PAGE_SIZE
1FFF constant PAGE_MASK
  10 constant REGS

80000000 constant FLG_V
40000000 constant FLG_C
20000000 constant FLG_Z
10000000 constant FLG_N

 0 constant ALU_A
 1 constant ALU_B
 2 constant ALU_INV_A
 3 constant ALU_AND
 4 constant ALU_OR
 5 constant ALU_XOR
 6 constant ALU_SHIFT_LEFT
 7 constant ALU_SHIFT_RIGHT
 8 constant ALU_MUL
 9 constant ALU_DIV
 A constant ALU_ADC
 B constant ALU_ADD
 C constant ALU_SBC
 D constant ALU_SUB

20 constant ALU_JUMP
21 constant ALU_LINK

30 constant ALU_GET_FLAGS
31 constant ALU_SET_FLAGS
32 constant ALU_GET_TRAPS
33 constant ALU_SET_TRAPS

40 constant ALU_LOAD_WORD
41 constant ALU_STORE_WORD
42 constant ALU_LOAD_BYTE
43 constant ALU_STORE_BYTE

50 constant ALU_TRAP
51 constant ALU_TLB_SINGLE
52 constant ALU_TLB_ALL
53 constant ALU_TLB_SET

1 constant R.OP
2 constant R.EXT
4 constant R.REL

:m ins> 0 ;m
:m op 30 lshift or ;m
:m >lit swap FFFFFFFF and or ;m

:m ADD ins> ALU_ADD op t, ;m
:m LIT ins> R.OP 24 lshift or ALU_A op >lit t, ;m
:m JMP ;m
:m STO ins> R.OP 24 lshift or ALU_STORE_WORD op >lit t, ;m
:m LOD ;m

\ 1 LIT IO_START PAGE_SIZE 8 + STO \ TRON
\ IO_START PAGE_SIZE 0 + STO \ HALT
0 t,

save-hex as.hex
bye
