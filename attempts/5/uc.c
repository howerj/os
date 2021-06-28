/* universal constructor; compile for a pascal like language 
 *
 * TODO: Decide on grammar, implement compiler, iterate.
 *
 * Needs: Modules (not needed initially), Functions (nested), Unsafe 
 * operations, arrays, byte/strings, records, function pointers, basic
 * arithmetic and bitwise operators, debugging and assert facilities,
 * optional garbage collection, coroutines (not needed initially),
 * public/private methods and members, recursion (guaranteed tail 
 * recursion?), floats Not needed initially).
 *
 * Stage-1: Specify grammar
 * Stage-2a: Implement lexer
 * Stage-2b: Implement parser
 * Stage-3: Code generation (relative jumps only)
 * Stage-4: Iterate development
 * Stage-5: Self hosting
 *
 *
 * use unsafe, io;
 *
 * type record x { int z; };
 * var z: x := { 0, };
 * function add(x, y: int): int {
 * 	return x + y;
 * }
 * begin
 *
 * end.
 */

int main(void) {
	return 0;
}
