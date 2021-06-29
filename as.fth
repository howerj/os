\ Richard James Howe, howe.r.j.89@gmail.com, Temporary Assembler, Public Domain
only forth definitions hex
1 20 lshift 2* 0= [if] .( 64-bit Forth needed ) cr abort [then]

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
:m there tdp @ ;m
:m tc! tflash + c! ;m
:m tc@ tflash + c@ ;m
:m t! tflash + ! ;m
:m t@ tflash + @ ;m
:m hex# ( u -- addr len ) 0 <# base @ >r hex =lf hold #S r> base ! #> ;m
:m save-hex ( <name> -- )
  parse-word w/o create-file throw
  there 0 ?do i t@  over >r hex# r> write-file throw tcell +loop
   close-file throw ;m

\ Instruction flags
8000 constant fJMP
4000 constant fREL
2000 constant fCAL
2000 constant fPSH \ NB. Reused when fJMP set
1000 constant fEXT
0800 constant fFV
0400 constant fFC
0200 constant fFZ
0100 constant fFN
0080 constant fPOP
\ ALU: Arithmetic
 0 constant iA
 1 constant iB
 2 constant iInvA
 3 constant iAnd
 4 constant iOr
 5 constant iXor
 6 constant iLshift
 7 constant iRshift
 8 constant iMul
 9 constant iDiv
 A constant iAdc
 B constant iAdd
 C constant iSbc
 D constant iSub
\ ALU: Registers
20 constant iPc
21 constant iSPc
22 constant iSp
23 constant iSSp
24 constant iFlg
25 constant iSFlg
26 constant iLvl
27 constant iSLvl
\ ALU: Load/Store
30 constant iLoadW
31 constant iLoadRelW
32 constant iStoreW
33 constant iStoreRelW
34 constant iLoadB
35 constant iLoadRelB
36 constant iStoreB
37 constant iStoreRelB
\ ALU: Miscellaneous
40 constant iTrap
\ ALU: MMU/TLB
50 constant iFlsh1
51 constant iFlshAll
52 constant iTlb

0000080000000000 constant MEMORY_START 
0000040000000000 constant IO_START     


save-hex as.hex
bye
