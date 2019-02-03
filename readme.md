# Project Oberon-like (Jorgen)

| Project   | Jorgen: An system like Project Oberon      |
| --------- | ------------------------------------------ |
| Author    | Richard James Howe                         |
| Copyright | 2019 Richard James Howe                    |
| License   | MIT                                        |
| Email     | howe.r.j.89@gmail.com                      |
| Website   | <https://github.com/howerj/vm>             |

**This is a work in progress, it is liable to not be complete, not work, or not
make sense**


	       _                            
	      | |                           
	      | | ___  _ __ __ _  ___ _ __  
	  _   | |/ _ \| '__/ _` |/ _ \ '_ \ 
	 | |__| | (_) | | | (_| |  __/ | | |
	  \____/ \___/|_|  \__, |\___|_| |_|
			    __/ |           
			   |___/            


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
* Port the system to an FPGA
  - My Forth CPU system <https://github.com/howerj/forth-cpu> could be used as
    basis for some of the peripherals

## To Do

* [ ] Add project documentation
* [ ] Implement a Virtual Machine
  * [ ] UART
  * [ ] Timer
  * [ ] Interrupts
  * [ ] Mass storage interface
* [ ] Make a simple compiler for it
  * [ ] Make test programs to exercise the virtual machine
  * [ ] Port trivial programs
* [ ] Improve Virtual Machine
  * [ ] Add Virtual Memory to the Virtual Machine
  * [ ] Graphics
  * [ ] Mouse
  * [ ] Sound
* [ ] Make a Kernel for the Virtual Machine

## Oberon Language

* Simplify certain things; no special character for private, use a capital
  first letter
* Add built in floating point numbers later, if at all, provide a library for Q
  numbers (Q32.32, or Q16.16).
* As many things as possible should be specified; the system integer width, the
  alignment of things in memory, how signed integers should wrap, etcetera. C
  is underspecified, and allows lots of undefined behaviour, this language
  would not.
* 64-bit only
* The language would have a much larger series of libraries, and language
features could be added as a library

## Major new language features:
* Multiple return values
* Function overloading
* Allow functions that are not nested to be returned from functions as a
  function pointer
* *optional* garbage collection; this could make it competitive with C
* A mechanism for specifying attributes; some the compiler may know about, for
  example to specify alignment, or to specify that a function is pure, some it 
  may not know about and would be user defined.
* Do not use shouty case for keywords
* UTF-8, enumerations (Maybe)

## References

* <http://www.projectoberon.com/>
* <https://en.wikipedia.org/wiki/Oberon_%28operating_system%29>
* <https://www.inf.ethz.ch/personal/wirth/ProjectOberon/index.html>
* <https://9p.io/plan9>
* <https://en.wikipedia.org/wiki/Plan_9_from_Bell_Labs>
* <https://en.wikipedia.org/wiki/PL/0>
* <https://github.com/mit-pdos/xv6-public>
* <https://en.wikipedia.org/wiki/Xv6>
* <https://pdos.csail.mit.edu/6.828/2018/xv6.html>
* <http://aiju.de/plan_9/plan9-syscalls>
* <https://en.wikipedia.org/wiki/MINIX>
* <https://www.minix3.org/>
* <https://wiki.osdev.org/Expanded_Main_Page>

Additional system calls:

* unveil only parts of the file system: <https://man.openbsd.org/unveil>
* mmap <http://man7.org/linux/man-pages/man2/mmap.2.html>
* pledge <https://man.openbsd.org/pledge.2>
* tame <https://man.openbsd.org/OpenBSD-5.8/man2/tame.2>


