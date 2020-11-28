# SYSTEM: Project Oberon-like system

| Project   | SYSTEM: An system like Project Oberon      |
| --------- | ------------------------------------------ |
| Author    | Richard James Howe                         |
| Copyright | 2020 Richard James Howe                    |
| License   | MIT                                        |
| Email     | howe.r.j.89@gmail.com                      |
| Website   | <https://github.com/howerj/vm>             |


**This is a work in progress, it is liable to not be complete, not work, or not
make sense**

# To Do: Phase 1

* [x] Get basic VM working
* [ ] Revisit VM design after making compiler/kernel
* [x] Make a assembler for the VM (now deleted)
* [ ] Make a pascal compiler for the VM
* [ ] Make a bootloader
* [ ] Start work on a Unix kernel for the VM

# Goals

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
* Make the system self hosting, this will involve making utilities to pack and
  unpack the disk image.
* Design a database to be used as an executable format, file system,
  archive format, and more. This should be similar to sqlite.
* Port the system to an FPGA
  - My Forth CPU system <https://github.com/howerj/forth-cpu> could be used as
    basis for some of the peripherals

# References

* <https://9p.io/plan9/>
* <https://en.wikipedia.org/wiki/Plan_9_from_Bell_Labs>
* <http://www.projectoberon.com/>
* <https://en.wikipedia.org/wiki/Oberon_%28operating_system%29>
* <https://people.inf.ethz.ch/wirth/ProjectOberon/index.html>
* <https://github.com/rswier/swieros>
* <https://github.com/howerj/forth-cpu>
* <http://oberon07.com/EBNF.txt>
* <https://6004.mit.edu/web/_static/fall20/resources/sp20/L13.pdf>
* <https://news.ycombinator.com/item?id=25238126> and <https://6004.mit.edu/web/fall20/resources/sp20>

