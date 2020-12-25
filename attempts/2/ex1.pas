(* example program 1, prints out square numbers *)
var x, squ;

procedure square;
begin
   squ := x * x
end;

begin
   x := 1;
   while x <= 10 do
   begin
      call square;
      write squ;
      x := x + 1
   end
end.

Also of note, this section is not read in by the compiler, so
we can write whatever we want here.

