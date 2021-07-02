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
:m tcells tcell * ;m
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
:m t, there t! =cell tdp +! ;m

\ Instruction flags
:m fJMP 8000 or ;m
:m fREL 4000 or ;m
:m fCAL 2000 or ;m
:m fPSH 2000 or ;m
:m fEXT 1000 or ;m
:m fFV 0800 or ;m
:m fFC 0400 or ;m
:m fFZ 0200 or ;m
:m fFN 0100 or ;m
:m fPOPB 0080 or ;m
:m fPOPA 0040 or ;m

:m iA  0 or ;m
:m iB  1 or ;m
:m iInvA  2 or ;m
:m iAnd  3 or ;m
:m iOr  4 or ;m
:m iXor  5 or ;m
:m iLshift  6 or ;m
:m iRshift  7 or ;m
:m iMul  8 or ;m
:m iDiv  9 or ;m
:m iAdc  A or ;m
:m iAdd  B or ;m
:m iSbc  C or ;m
:m iSub  D or ;m

:m iPc 20 or ;m
:m iSPc 21 or ;m
:m iSp 22 or ;m
:m iSSp 23 or ;m
:m iFlg 24 or ;m
:m iSFlg 25 or ;m
:m iLvl 26 or ;m
:m iSLvl 27 or ;m

:m iLoadW 30 or ;m
:m iStoreW 31 or ;m
:m iLoadB 32 or ;m
:m iStoreB 33 or ;m
:m iSignal 34 or ;m
:m iSSignal 35 or ;m

:m iTrap 36 or ;m
:m iFlsh1 37 or ;m
:m iFlshAll 38 or ;m
:m iTlb 39 or ;m

0000080000000000 constant MEMORY_START 
0000040000000000 constant IO_START     
2000             constant PAGE_SIZE

:m ins> 0 ;m
:m >ins 30 lshift swap FFFFFFFFFFFF and or ;m ( op1 op -- instr )

:m j ins> fJMP >ins t, ;m
:m jr ins> fEXT fJMP fREL >ins t, ;m
:m j.z ins> fJMP fFZ >ins t, ;m
:m jr.z ins> fEXT fJMP fREL fFZ >ins t, ;m
:m sto ins> iStoreW >ins t, ;m
:m push ins> fPSH iB >ins t, ;m
:m -push ins> fPSH fEXT iB >ins t, ;m
:m add ins> iAdd >ins t, ;m
:m sub ins> iSub >ins t, ;m

:m begin there ;m
:m again there - jr ;m
:m until there - jr.z ;m
:m if there 0 t, ;m
:m then there - ins> fEXT fJMP fREL fFZ >ins there swap t! ;m
\ :m while if swap ;m
\ :m repeat ;m

:m halt 1 push IO_START PAGE_SIZE + sto ;m
:m tron 1 push IO_START PAGE_SIZE + tcell + sto ;m

:m #-1 -1 push ;m
:m #0   0 push ;m

:m 0= tcell jr.z #-1 #0 ;m

tron
10 add
begin
  2 sub 0=
until
halt

save-hex as.hex
bye
