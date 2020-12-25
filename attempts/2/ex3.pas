(* example program 3; this is not a coherent program, just test code *)

const x = 3, y = 4;
var z, sx, sy;

(* syntax for function argument declaration is non functional at the moment *)
procedure square, x;
begin
	sy := sx * sx;
end;

begin
	z := 3;
	do z:= z+1 while z < 10;
	z := -z;
	write z;
	read z;
	write z;
	write x;
	(* doesn't do anything meaning full at the moment, in fact
	   the code generation is incorrect *)
	z := call square, x; 
	z := (z * z + (z - z));

	(* syntax for function call arguments is non functional *)
	call square, x;
	if y = 4 then
		z := 3
	else 
		write 99;
end.
