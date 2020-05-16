# SYSTEM: Project Oberon-like system

| Project   | SYSTEM: An system like Project Oberon      |
| --------- | ------------------------------------------ |
| Author    | Richard James Howe                         |
| Copyright | 2020 Richard James Howe                    |
| License   | BSD (Zero Clause)                          |
| Email     | howe.r.j.89@gmail.com                      |
| Website   | <https://github.com/howerj/vm>             |


**This is a work in progress, it is liable to not be complete, not work, or not
make sense**

Project goals:
* Make an Oberon like language that compiles to a virtual machine that could,
  in principle, be ported to an FPGA.
  - The language should have *optional* garbage collection, manual
    allocation/deallocation, or none.
  - There may be a few extensions (attributes, reflection, multiple return
  values, more tools for proofs and assertions).
* Design a Unix like kernel
  - Similar to Plan 9
  - With a few more security related system calls:
    - unveil
    - restrict future system calls
  - Make a busybox like executable which contains the basic utilities, the
  system compiler, a TCL like system shell, an assembler, a Forth interpreter,
  and more!
* Port the system to an FPGA
  - My Forth CPU system <https://github.com/howerj/forth-cpu> could be used as
    basis for some of the peripherals

