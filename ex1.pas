(* Test program *)
(*
TODO
	- syntax for module declaration and import
	- add syntax for type declaration, arrays, pointers, records
	- add syntax for multiple return arguments?
	- basic code generation
	- make a useful test program
	- make a vim syntax file for this
*)

module test;
import system;

const a:c = 3, c:string = "hello";
var z:uint, y:uint;

procedure square (a, b) {
	var r;
	{
		r := a * b
	}
}

procedure fn2:uint (b:uint, c:uint) {
}

{
	a := 4 / 3 + 9;
	;
	a := 9;
	while a # 18 { a := a + 1 };

	if a = 0 {
	};
	

	if a = 1 {
	} else if a = 2 {
	} else if a = 3 {
	} else if a = 4 {
	};

}.

Anything goes here.

