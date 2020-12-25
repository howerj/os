/** @file       debug.c
 *  @brief      PL/0 debugging and error routines
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com 
 *
 *  @todo a printf like function for printing out data structures would be
 *        useful */


#include "pl0.h"
#include <assert.h>
#include <inttypes.h>

static const char *inames[] = {
#define X(ENUM, NAME) [ENUM] = NAME,
	XMACRO_INSTRUCTION
#undef X
	NULL
};

int _syntax_error(lexer_t *l, const char *file, const char *func, unsigned line, const char *msg)
{
	assert(l);
	assert(file);
	assert(func);
	assert(msg);
	fprintf(stderr, "%s:%s:%u\n", file, func, line);
	fprintf(stderr, "  syntax error on line %u of input\n  %s\n", l->line, msg);
	print_token(stderr, l->token, 2);
	ethrow(&l->error);
	return 0;
}

int instruction_dump(code_t *c, FILE *output, int nprint, unsigned i)
{ /* print out an instruction and any of its operands */
	instruction op;
	assert(c);
	assert(output);
	op = c->m[i];
	fprintf(output, "%03x: %03x %s\n", i, op, op <= IHALT ? inames[op] : "invalid op");
	if(nprint && c->root)
		print_node(output, c->debug[i], 1, 2);
	if(op == ILOAD || op == ISTORE || op == IVSTORE || op == IVLOAD ||
	op == ICALL || op == IJMP || op == IJZ || op == IPUSH || op == IREAD) {
		i++;
		fprintf(output, "%03x: %03"PRIxPTR" data\n", i, c->m[i]);
	}
	return i;
}

void dump(code_t *c, FILE *output, int nprint)
{ 
	unsigned i;
	assert(c);
	assert(output);
	fputs("disassembly:\n", output);
	for(i = 0; i < c->here; i++)
		i = instruction_dump(c, output, nprint, i);
	fputs("symbols defined:\n", output);
	for(i = c->size - 1; i > c->globals; i--) /**@todo lookup variable names */
		fprintf(output, "%03x: %"PRIiPTR"\n", i, c->m[i]);
}


