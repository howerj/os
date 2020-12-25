/** @file       vm.c
 *  @brief      PL/0 virtual machine
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com 
 *  @todo print out stack frames */

#include "pl0.h"
#include <assert.h>
#include <inttypes.h>

static intptr_t base(intptr_t *s, intptr_t b, intptr_t l)
{
	while(l > 0) {
		b = s[b];
		l--;
	}
	return b;
}

int vm(code_t *c, FILE *input, FILE *output, int debug)
{
	intptr_t * const m = c->m;
	intptr_t stack[MAX_STACK] = { 0 };
	intptr_t *S = stack, /* variable stack pointer */
		 *pc = &c->m[0], 
		 f = 0,
		 frame = 0, /* frame pointer */
		 op;
	assert(c);
	assert(input);
	assert(output);
	if(debug)
		fprintf(stderr, "running vm:\n");

LOOP:
	assert(pc >= m);
	assert(pc <= m + c->size);
	assert(S >= stack - 1);
	assert(S < stack + MAX_STACK);
	if(debug)
		instruction_dump(c, output, debug > 1, (unsigned)(pc - m));
	switch(op = *pc++) {
	case INOP:                                 break;
	case ILOAD:   *++S = f; f = m[*pc++];      break;
	case ISTORE:  m[*pc++] = f; f = *S--;      break;
	/**@todo change this into a two operand instruction with a base level
	 * so nested procedures work correctly */
	case IVLOAD:  *++S = f; f = stack[(frame - *pc++)];
		      break;
	case IVSTORE: stack[(frame - *pc++)] = f; f = *S--;  
		      break;
	case ICALL:   *++S = f;
		      *++S = frame;
		      f = (intptr_t)(pc+1); 
		      pc = m+*pc;
		      frame = S - stack;
		      break;
	case IRETURN: pc = (intptr_t*)f; 
		      frame = *S--;
		      S = stack + frame + 1;
		      f = *S--; 
		      break;
	case IJMP:    pc = m+*pc;              break;
	case IJZ:     pc = f ? pc+1  : m+*pc; f = *S--; break;
	case IJNZ:    pc = f ? m+*pc : pc+1;  f = *S--; break;
	case IADD:    f = *S-- +  f;           break;
	case ISUB:    f = *S-- -  f;           break;
	case IMUL:    f = *S-- *  f;           break;
	case IDIV:    
		if(!f || (f == -1 && *S == INTPTR_MIN)) { 
			fprintf(stderr, "divide by zero!\n");
			return VM_ERROR_DIV0;
		}
		f = *S-- /  f;           
		break;
	case ILTE:    f = *S-- <= f;           break;
	case IGTE:    f = *S-- >= f;           break;
	case ILT:     f = *S-- <  f;           break;
	case IGT:     f = *S-- >  f;           break;
	case IEQ:     f = *S-- == f;           break;
	case INEQ:    f = *S-- != f;           break;
	case IAND:    f = *S-- &  f;           break;     
	case IOR:     f = *S-- |  f;           break;      
	case IXOR:    f = *S-- ^  f;           break;    
	case IINVERT: f = ~f;                  break; 
	case INEGATE: f = -f;                  break; 
	case IODD:    f = f & 1;               break;
	case IPUSH:   *++S = f; f = *pc++;     break;
	case IPOP:    f = *S--;                break;
	case IREAD:   
		if(fscanf(input, "%"PRIiPTR, &m[*pc++]) != 1)
			return VM_ERROR_READ;
		break;
	case IWRITE:  
		if(fprintf(output, "%"PRIiPTR"\n", f) < 0)
			return VM_ERROR_WRITE;
		f = *S--; 
		break;
	case IHALT: return VM_ERROR_OK;
	default:
		fprintf(stderr, "illegal operation (%"PRIdPTR")!\n", op);
		return VM_ERROR_ILLEGAL_OP;
	}
	goto LOOP;
}

