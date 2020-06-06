(* @bug function arguments are the wrong way around on the stack! 
   @todo allocate local variables *)
var gx, gy;

procedure nesttest, z, t;
begin
	write z;
	write t;
	gy := z + t;
	write gy;
end;

procedure argtest, x, y;
begin
	write x;
	write y;
	y := y + 150;
	gx := y;
	call nesttest, x, y;
end;

begin
	call argtest, gx, 1000;
	(* gy := call argtest, gx, gy;*)
end.
