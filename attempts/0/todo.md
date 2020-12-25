# todo.md

* Refactor
	- Remove all the magic numbers
* Clean up code
* Keyboard code
	- Once the keyboard has been integrate a small FORTH
	interpreter can be added to the kernel, as a test of
	everything so far and as a kernel level command line.
* Virtual code
	- Each device (Keyboard, Serial Port, VGA, ...) should have
	a device node interface.
	- The kernel should have a configuration and process interface
* File system code
* Translate all NASM code into gas syntax, removing the dependency on NASM.
