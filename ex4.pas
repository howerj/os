(* This is for testing nested procedures, which do not work at the moment,
or at least only partially, the first nested procedure gets called instead *)
var x;
procedure nesting, x, y;
	var z;
	procedure nested;
	begin
		write z;
		write x;
		write y;
		write 65;
		z := 3;
		write z;
	end;
begin
	write 99;
	call nesting, 3, 4;
end;

begin
	x := 80;
	call nesting, x, x;
end.
