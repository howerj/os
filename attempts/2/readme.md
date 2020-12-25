# Toy Compiler

This is a project for a toy compiler for a [pascal][] like language called 
[PL/0][]. It will evolve into a more useful language and have multiple targets
(a [p-code][] machine and a [Forth][] virtual machine, [libforth][]).

**This is an old project of time, it is not active nor will it be developed**

## To Do

* Flesh out the language
  - Both the grammar and semantics of the language need working out.
* Make targets for [libforth][] and [forth-cpu][].

### Language To Do

To do list, roughly in order.

* Get function calls with arguments working
* Get nested functions working
* Add optional arguments to functions
* Write out the full grammar
* Add more control structures:

	do statements while condition;
	unless statement else statment
	statement (if | while | unless) condition;

* Add arrays
* Add records
* Module system
* Dump compiled core to disk and allow loading images
* Add mechanism for inline assembly something like:

	assembler,
		push 4,
		call function,
		label-name,
		push 10,
		$label-name,
		push 1,
		sub,
		jnz label-name;

## See also

* <http://www.ethoberon.ethz.ch/WirthPubl/CBEAll.pdf>

[pascal]: https://en.wikipedia.org/wiki/Pascal_%28programming_language%29
[PL/0]: https://en.wikipedia.org/wiki/PL/0
[p-code]: https://en.wikipedia.org/wiki/P-code_machine
[Forth]: https://en.wikipedia.org/wiki/Forth_%28programming_language%29
[libforth]: https://github.com/howerj/libforth
[forth-cpu]: https://github.com/howerj/forth-cpu

