(* Test program *)
(*
TODO
	- Add shebang syntax so these programs can be ran as scripts
	- syntax for module declaration and import
	- add syntax for type declaration, arrays, pointers, records
	- basic code generation
	- make a useful test program
	- make a vim syntax file for this
*)

module test;
import system;

type time = uint, seconds = b, rec = record { a: uint }, pnt = pointer to uint, ar = array 4 of uint;
const a:c = 3, c:string = "hello";
var z:uint, y:uint;

procedure square (a, b) {
	var r;
	r := a * b;
}

procedure fn2 (b:uint, c:uint) : uint {
}

a := 4 / 3 + 9;
;
a := 9;
while a # 18 { a := a + 1 };

square (4);

a.b[2]:=2;

if square(4) = 16 { };

if a = 1 { }
else if a = 2 { }
else if a = 3 { }
else if a = 4 { };

.

Anything goes here.

