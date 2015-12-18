# README.md
## README file for a small hobby OS

Initial kernel from:

<http://wiki.osdev.org/Bare_Bones>

Using:

<http://wiki.osdev.org/GCC_Cross-Compiler>

On:

Linux work 3.2.0-4-686-pae #1 SMP Debian 3.2.65-1+deb7u1 i686 GNU/Linux

(from uname -a)

Aka Debian 7.x

And:

binutils-2.24
gcc-4.9.2

The other libraries were installed on the system.

# Plan

The eventual plan will be to create a plan 9 / Unix v7 clone with a lisp
oriented user land. Although that is in the long term.

The short term I plan on getting a better VGA text driver up and running,
keyboard and floppy disk. Once that is done a small FORTH interpreter will
be made as a sort of kernel shell and debugging interface before proceeding
with the rest of the system.

I will copy code over from the xv6/ repository once I understand the module,
then I will incorporate this code into this project.

Other tutorials should be used as well:

<http://www.jamesmolloy.co.uk/tutorial_html/>
<http://wiki.osdev.org/James_Molloy%27s_Tutorial_Known_Bugs#Problem:_Inline_Assembly>
<http://wiki.osdev.org/Barebones>

* Keyboard
* Replace grub boot loader

