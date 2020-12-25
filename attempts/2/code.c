/** @file       code.c
 *  @brief      PL/0 code generation file
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com 
 *
 * @todo allocate globals on the stack as well, this should simplify the
 * code once nested procedures have been implemented.
 */

#include "pl0.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static void generate(code_t *c, node_t *n, instruction i)
{
	assert(c);
	assert(n);
	c->m[c->here++] = i;
	if(c->root)
		c->debug[c->here] = n;
}

static unsigned hole(code_t *c) 
{
	assert(c);
	return c->here++;
}

static unsigned newvar(code_t *c)
{
	assert(c);
	return c->globals--;
}

static void fix(code_t *c, unsigned hole, unsigned patch)
{
	assert(c);
	c->m[hole] = patch;
}

static code_t *new_code(unsigned size, int debug)
{
	code_t *r;
	assert(size);
	r = allocate(sizeof(*r)+size*sizeof(r->m[0]));
	if(debug)
		r->debug = allocate(size*sizeof(*r->debug));
	r->size = size;
	r->globals = size - 1; /* data stored at end of core*/
	return r;
}

void free_code(code_t *c)
{
	if(!c)
		return;
	free_node(c->root);
	free(c->debug);
	free(c->patches);
	free(c);
}

void add_patch(code_t *c, intptr_t location, intptr_t value)
{
	assert(c);
	assert(location >= 0);
	assert(location < c->size);
	c->patchcount++;
	c->patches = realloc(c->patches, sizeof(*(c->patches)) * c->patchcount);
	if(!(c->patches)) {
		fprintf(stderr, "reallocate failed\n");
		abort();
	}
	c->patches[c->patchcount - 1].location = location;
	c->patches[c->patchcount - 1].value    = value;
}

void patch(code_t *c)
{
	size_t i;
	assert(c);
	for(i = 0; i < c->patchcount; i++) {
		printf("patching %"PRIdPTR " -> %"PRIdPTR"\n",
				c->patches[i].location,
				c->patches[i].value);
		c->m[c->patches[i].location] = c->patches[i].value;
	}
}

static scope_t *new_scope(scope_t *parent)
{
	scope_t *r = allocate(sizeof(*r));
	r->parent = parent;
	r->allocated = 1;
	return r;
}

static void free_scope(scope_t *s)
{
	if(!s)
		return;
	free(s);
}

static void _code_error(code_t *c, token_t *t, const char *file, const char *func, unsigned line, const char *msg)
{
	assert(c);
	assert(t);
	assert(file);
	assert(func);
	assert(msg);
	fprintf(stderr, "error (%s:%s:%u)\n", file, func, line);
	fprintf(stderr, "identifier '%s' on line %u: %s\n", t->p.id, t->line, msg);
	ethrow(&c->error);
}

#define code_error(CODE, TOKEN, MSG) _code_error((CODE),(TOKEN), __FILE__, __func__, __LINE__, (MSG))

static instruction token2code(code_t *c, token_t *t)
{
	instruction i;
	assert(c);
	assert(t);
	i = IHALT;
	switch(t->type) {
	case LESSEQUAL:    i = ILTE; break;
	case GREATEREQUAL: i = IGTE; break;
	case ODD:          i = IODD; break; /**@todo translate to other instructions */
	case INVERT:       i = IINVERT; break; 
	case NOTEQUAL:     i = INEQ; break;
	case MUL:          i = IMUL; break;
	case SUB:          i = ISUB; break;
	case DIV:          i = IDIV; break;
	case ADD:          i = IADD; break;
	case LESS:         i = ILT;  break;
	case EQUAL:        i = IEQ;  break;
	case GREATER:      i = IGT;  break;
	case AND:          i = IAND; break;
	case OR:           i = IOR;  break;
	case XOR:          i = IXOR; break;
	default:  fprintf(stderr, "invalid conversion");
		  ethrow(&c->error);
	}
	return i;
}

static instruction unary2code(code_t *c, token_t *t)
{
	instruction i;
	assert(c);
	assert(t);
       	i = IHALT;
	switch(t->type) {
	case ADD:   i = INOP;    break;
	case SUB:   i = INEGATE; break;
	case ODD:   i = IODD;    break;
	default:  fprintf(stderr, "invalid conversion");
		  ethrow(&c->error);
	}
	return i;
}

static token_t *finder(node_t *n, token_t *t, node_t **found)
{
	if(!n || !n->token) {
		*found = NULL;
		return NULL;
	}
	if(!strcmp(n->token->p.id, t->p.id)) {
		*found = n;
		return n->value ? n->value : n->token; /* constant or variable */
	}
	return finder(n->token->procedure ? n->o[1] : n->o[0], t, found);
}

/* find token and node it belongs to */
static token_t *find(scope_t *s, token_t *t, node_t **out_node)
{
	token_t *found;
	assert(s);
	assert(t);
	if((found = finder(s->constants, t, out_node)))
		return found;
	if((found = finder(s->variables, t, out_node)))
		return found;
	if((found = finder(s->functions, t, out_node)))
		return found;
	if(s->parent)
		return find(s->parent, t, out_node);
	*out_node = NULL;
	return NULL;
}

static void allocvar(code_t *c, node_t *n, int global, unsigned level, scope_t *scope)
{
	unsigned v;
	assert(scope);
	assert(c);
	if(!n)
		return;
	if(global) { /**@todo move this onto the stack */
		v = newvar(c);
		n->token->location = v;
	} else {
		n->token->location = scope->allocated++;
	}
	n->token->located = 1;
	n->token->global = global;
	n->token->level = level;
	allocvar(c, n->o[0], global, level, scope);
}

static void _code(code_t *c, node_t *n, scope_t *parent, unsigned level) 
{
	unsigned hole1, hole2;
	scope_t *current = NULL;
	token_t *found;
	node_t  *nfound= NULL;
	if(!n)
		return;
	if(c->root)
		c->debug[c->here] = n;
	switch(n->type) {
	case PROGRAM: 
		_code(c, n->o[0], NULL, level); 
		generate(c, n, IHALT); 
		break;
	case BLOCK: 
		current = new_scope(parent);
		_code(c, n->o[0], current, level + 1); /*constants*/
		_code(c, n->o[1], current, level + 1); /*variables*/
		if(!parent) {
			generate(c, n, IJMP);
			hole1 = hole(c);
		}
		_code(c, n->o[2], current, level + 1); /*procedures*/
		if(!parent)
			fix(c, hole1, c->here);
		_code(c, n->o[3], current, level + 1); /*final statement*/
		free_scope(current);
		break;
	case CONSTLIST: 
		parent->constants = n; 
		break;
	case VARLIST:   
		parent->variables = n; 
		allocvar(c, n, parent->parent == NULL, level, parent); 
		break;
	case PROCLIST: 
		current = new_scope(parent);
		if(!parent->functions)
			parent->functions = n;
		parent->current = n;
		n->token->location = c->here;
		n->token->located = 1;
		found = find(parent, n->token, &nfound);
		if(found != n->token) {
			fprintf(stderr, "previous definition on %u\n", found->line);
			code_error(c, n->token, "function defined twice");
		}
		_code(c, n->o[2], current, level + 1); /* allocate variables */
		_code(c, n->o[0], current, level + 1); /* block */
		generate(c, n, IRETURN);
		_code(c, n->o[1], current, level + 1); /* next procedure */
		free_scope(current);
		break;
	case STATEMENT:  /*do nothing, empty statement*/
		break;
	case ASSIGNMENT:
		_code(c, n->o[0], parent, level);
		if(!(found = find(parent, n->token, &nfound)))
			code_error(c, n->token, "variable not found"); 
		if(found->procedure || found->constant)
			code_error(c, n->token, "not a variable");
		generate(c, n, found->global ? ISTORE : IVSTORE);
		generate(c, n, found->location);
		break;
	case UNARY_EXPR_LIST:
		_code(c, n->o[0], parent, level);
		_code(c, n->o[1], parent, level);
		break;
	case INVOKE:
		if(!(found = find(parent, n->token, &nfound))) 
			code_error(c, n->token, "function not found");
		if(!found->procedure)
			code_error(c, n->token, "variable is not a procedure");
		if(!found->located)
			code_error(c, n->token, "forward references not allowed");
		if(nfound->argc != n->argc) {
			fprintf(stderr, "%zu != %zu\n", nfound->argc, n->argc);
			code_error(c, n->token, "expected and actual argument counts differ");
		}
		_code(c, n->o[0], parent, level); /* arguments to function */
		generate(c, n, ICALL); 
		generate(c, n, found->location);
		break;
	case OUTPUT:  
		_code(c, n->o[0], parent, level); 
		generate(c, n, IWRITE); 
		break;
	case INPUT:       
		if(!(found = find(parent, n->token, &nfound)))
			code_error(c, n->token, "variable not found");
		if(found->procedure || found->constant)
			code_error(c, n->token, "not a variable");
		generate(c, n, IREAD); 
		generate(c, n, found->location); 
		break;
	case CONDITIONAL: 
		_code(c, n->o[0], parent, level); 
		generate(c, n, IJZ); 
		hole1 = hole(c);
		_code(c, n->o[1], parent, level); 
		if(n->o[2]) { /* if ... then ... else */
			generate(c, n, IJMP);
			hole2 = hole(c);
			fix(c, hole1, c->here);
			_code(c, n->o[2], parent, level);
			fix(c, hole2, c->here);
		} else { /* if ... then */
			fix(c, hole1, c->here); 
		}
		break;
	case CONDITION:   
		if(n->token && n->token->type == ODD) {
			_code(c, n->o[0], parent, level);
			generate(c, n, IODD);
		} else {
			_code(c, n->o[0], parent, level);
			_code(c, n->o[1], parent, level);
			generate(c, n, token2code(c, n->token));
		}
		break;
	case WHILST:      
		hole1 = c->here;
		_code(c, n->o[0], parent, level);
		generate(c, n, IJZ);
		hole2 = hole(c);
		_code(c, n->o[1], parent, level);
		generate(c, n, IJMP);
		fix(c, hole(c), hole1);
		fix(c, hole2, c->here);
		break;
	case DOOP:
		hole1 = c->here;
		_code(c, n->o[0], parent, level);
		_code(c, n->o[1], parent, level);
		generate(c, n, IJNZ);
		fix(c, hole(c), hole1);
		break;
	case LIST:        
		_code(c, n->o[0], parent, level); 
		_code(c, n->o[1], parent, level); 
		break;
	case UNARY_EXPRESSION:
		/**@todo unary expression not handled correctly */
		_code(c, n->o[0], parent, level);
		_code(c, n->o[1], parent, level);
		if(n->token) {
			instruction i = unary2code(c, n->token);
			if(i != INOP)
				generate(c, n, i);
		}
		break;
	case EXPRESSION: 
		_code(c, n->o[0], parent, level);
		generate(c, n, token2code(c, n->token));
		break;
	case TERM:
		 _code(c, n->o[0], parent, level);
		 _code(c, n->o[1], parent, level);
		 if(n->token)
			generate(c, n, token2code(c, n->token));
		 break;
	case FACTOR:
		if(!n->token) {
			_code(c, n->o[0], parent, level);
			return;
		}
		if(n->token->type == NUMBER) {
			generate(c, n, IPUSH);
			generate(c, n, n->token->p.number);
		} else if((found = find(parent, n->token, &nfound))) {
			if(found->procedure)
				code_error(c, n->token, "not a variable or constant");
			if(found->type == NUMBER) { /* find returns constants value */
				generate(c, n, IPUSH);
				generate(c, n, found->p.number);
			} else {
				assert(found->type == IDENTIFIER);
				generate(c, n, found->global ? ILOAD : IVLOAD); 
				generate(c, n, found->location);
			}
		} else {
			code_error(c, n->token, "variable not found");
		}
		break;
	}
}

code_t *code(node_t *n, size_t size, int debug)
{
	code_t *c = new_code(size, debug);
	if(debug)
		c->root = n;
	c->error.jmp_buf_valid = 1;
	if(setjmp(c->error.j)) {
		free_code(c);
		return NULL;
	}
	_code(c, n, NULL, 0);
	patch(c);
	return c;
}

static void scope(scope_t *s, FILE *output)
{
	if(!s)
		return;
	if(s->current && s->current->token)
		fprintf(output, "%s.", s->current->token->p.id);
	scope(s->parent, output);
}

static void print_sym(node_t *n, scope_t *parent, FILE *output)
{
	int isprocedure = n->token->procedure;
	fprintf(output, "%03x: %s ", n->token->location, isprocedure ? "func" : "var ");
	scope(isprocedure ? parent->parent : parent, output);
	fprintf(output, "%s\n", n->token->p.id);
}

static void _export(node_t *n, scope_t *parent, FILE *output) 
{
	node_t *x;
	scope_t *current = NULL;
	if(!n)
		return;
	switch(n->type) {
	case PROGRAM: 
		_export(n->o[0], NULL, output); break;
	case BLOCK: 
		current = new_scope(parent);
		_export(n->o[1], current, output); /*variables*/
		_export(n->o[2], current, output); /*procedures*/
		free_scope(current);
		break;
	case VARLIST:   
		parent->variables = n; 
		for(x = n; x; x = x->o[0])
			print_sym(x, parent, output);
		break;
	case PROCLIST:  
		if(!parent->functions)
			parent->functions = n;
		parent->current = n;
		print_sym(n, parent, output);
		_export(n->o[0], parent, output);
		_export(n->o[1], parent, output);
		break;
	default:
		return;
	}
}

void export(node_t *n, FILE *output)
{ /* export a list of symbols */
	assert(n);
	assert(output);
	_export(n, NULL, output);
}


