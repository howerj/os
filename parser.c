/** @file       parser.c
 *  @brief      PL/0 parser
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com */

#include "pl0.h"
#include <assert.h>
#include <stdlib.h>

static const char *names[] = {
#define X(ENUM, NAME) [ENUM] = NAME,
	XMACRO_PARSE
#undef X
	NULL
};

node_t *new_node(lexer_t *l, parse_e type, unsigned char size)
{
	node_t *r = allocate(sizeof(*r) + sizeof(r->o[0])*size);
	assert(l);
	assert(type < LAST_PARSE);
	if(l->debug)
		fprintf(stderr, "new> %s\n", names[type]);
	r->length = size;
	r->type = type;
	return r;
}

void free_node(node_t *n)
{
	if(!n)
		return;
	for(unsigned i = 0; i < n->length; i++)
		free_node(n->o[i]);
	free_token(n->token);
	free(n);
}

void print_node(FILE *output, node_t *n, int shallow, unsigned depth)
{
	if(!n)
		return;
	assert(output);
	indent(output, ' ', depth); 
	fprintf(output, "node(%d): %s\n", n->type, names[n->type]);
	print_token(output, n->token, depth);
	print_token(output, n->value, depth);
	if(shallow)
		return;
	for(size_t i = 0; i < n->length; i++)
		print_node(output, n->o[i], shallow, depth+1);
}

int accept(lexer_t *l, token_e sym)
{
	assert(l);
	if(sym == l->token->type) {
		free_token(l->accepted); /* free token owned by lexer */
		l->accepted = l->token;
		if(sym != EOI && sym != DOT)
			lexer(l);
		return 1;
	}
	return 0;
}

static void use(lexer_t *l, node_t *n)
{ /* move ownership of token from lexer to parse tree */
	assert(l);
	assert(n);
	n->token = l->accepted;
	l->accepted = NULL;
}

static int _expect(lexer_t *l, token_e sym, const char *file, const char *func, unsigned line)
{
	assert(l);
	assert(file);
	assert(func);
	if(accept(l, sym))
		return 1;
	fprintf(stderr, "%s:%s:%u\n", file, func, line);
	fprintf(stderr, "  Syntax error: unexpected token\n  Got:          ");
	print_token(stderr, l->token, 0);
	fputs("  Expected:     ", stderr);
	print_token_enum(stderr, sym);
	fprintf(stderr, "\n  On line: %u\n", l->line);
	ethrow(&l->error);
	return 0;
}

#define expect(L, SYM) _expect((L), (SYM), __FILE__, __func__, __LINE__)

static node_t *unary_expression(lexer_t *l);

static node_t *factor(lexer_t *l) /* ident | number | "(" unary-expression ")". */
{
	node_t *r = new_node(l, FACTOR, 1);
	if(accept(l, IDENTIFIER) || accept(l, NUMBER)) { 
		use(l, r);
		return r;
	} else if(accept(l, LPAR)) {
		r->o[0] = unary_expression(l);
		expect(l, RPAR);
	} else {
		syntax_error(l, "expected id, number or \"(\" unary-expression \")\"");
	}
	return r;
}

static node_t *term(lexer_t *l) /* factor {("*"|"/") factor}. */
{
	node_t *r = new_node(l, TERM, 2);
	r->o[0] = factor(l);
	if(accept(l, MUL) || accept(l, DIV)) {
		use(l, r);
		r->o[1] = factor(l);
	}
	return r;
}

static node_t *expression(lexer_t *l) /* { ("+"|"-") term}. */
{
	if(accept(l, ADD) || accept(l, SUB) || accept(l, AND) || accept(l, OR) || accept(l, XOR)) {
		node_t *r = new_node(l, EXPRESSION, 1);
		use(l, r);
		r->o[0] = term(l);
		return r;
	} else {
		return NULL;
	}
}

static node_t *unary_expression(lexer_t *l) /* [ "+"|"-"] term expression. */
{
	node_t *r = new_node(l, UNARY_EXPRESSION, 2);
	if(accept(l, ADD) || accept(l, SUB)) {
		if(l->accepted->type != ADD)
			use(l, r);
	}
	r->o[0] = term(l);
	r->o[1] = expression(l);
	return r;
}

static node_t *condition(lexer_t *l)
{
	node_t *r = new_node(l, CONDITION, 2);
	if(accept(l, ODD)) { /* "odd" unary_expression */
		use(l, r);
		r->o[0] = unary_expression(l);
	} else { /* unary_expression ("="|"#"|"<"|"<="|">"|">=") unary_expression*/
		r->o[0] = unary_expression(l);
		if(accept(l, EQUAL) || accept(l, GREATER) || accept(l, LESS) 
		|| accept(l, LESSEQUAL) || accept(l, GREATEREQUAL) || accept(l, NOTEQUAL)) { 
			use(l, r);
			r->o[1] = unary_expression(l);
		} else {
			syntax_error(l, "expected condition statement");
		}
	}
	return r;
}

static node_t *statement(lexer_t *l);

static node_t *list(lexer_t *l) /* statement {"," statement } */
{
	node_t *r = new_node(l, LIST, 2);
	r->o[0] = statement(l);
	if(accept(l, SEMICOLON))
		r->o[1] = list(l);
	return r;
}

static node_t *varlist(lexer_t *l) /* ident {"," ident} ";"  */
{
	node_t *r = new_node(l, VARLIST, 1);
	expect(l, IDENTIFIER);
	use(l, r);
	if(accept(l, COMMA))
		r->o[0] = varlist(l);
	return r;
}

static node_t *unary_expression_list(lexer_t *l) /* unary-expression {"," unary-expression } */
{
	node_t *r = new_node(l, UNARY_EXPR_LIST, 2);
	r->o[0] = unary_expression(l);
	if(accept(l, COMMA))
		r->o[1] = unary_expression_list(l);
	return r;
}

static size_t count(node_t *n, size_t list_member)
{
	size_t argc;
	assert(n);
	for(argc = 0; n; n = n->o[list_member], argc++)
		;
	return argc;
}

static node_t *call(lexer_t *l) /* ident "," unary_expression_list */
{
	node_t *r = new_node(l, INVOKE, 1);
	expect(l, IDENTIFIER);
	use(l, r);
	if(accept(l, COMMA)) {
		r->o[0] = unary_expression_list(l);
		r->argc = count(r->o[0], 1);
	}
	return r;
}

static node_t *statement(lexer_t *l)
{
	node_t *r = new_node(l, STATEMENT, 3);
	if(accept(l, IDENTIFIER)) { /* ident ":=" unary_expression */
		use(l, r);
		expect(l, ASSIGN);
		if(accept(l, CALL))
			r->o[0] = call(l);
		else
			r->o[0] = unary_expression(l);
		r->type = ASSIGNMENT;
	} else if(accept(l, CALL)) { /* "call" ident  */
		expect(l, IDENTIFIER);
		use(l, r);
		if(accept(l, COMMA)) {
			r->o[0] = unary_expression_list(l);
			r->argc = count(r->o[0], 1);
		}
		r->type = INVOKE;
	} else if(accept(l, READ)) { /* "read" ident */
		expect(l, IDENTIFIER);
		use(l, r);
		r->type = INPUT;
	} else if(accept(l, WRITE)) { /* "write" expression */
		r->o[0] = unary_expression(l);
		r->type = OUTPUT;
	} else if(accept(l, BEGIN)) { /* "begin" statement {";" statement } "end" */
		r->o[0] = list(l);
		expect(l, END);
		r->type = LIST;
	} else if(accept(l, IF)) { /* "if" condition "then" statement [ "else" statement ] */
		r->o[0] = condition(l);
		expect(l, THEN);
		r->o[1] = statement(l);
		if(accept(l, ELSE))
			r->o[2] = statement(l);
		r->type = CONDITIONAL;
	} else if(accept(l, WHILE)) { /*  "while" condition "do" statement */
		r->o[0] = condition(l);
		expect(l, DO);
		r->o[1] = statement(l);
		r->type = WHILST;
	} else if(accept(l, DO)) { /* "do" statement "while" condition */
		r->o[0] = statement(l);
		expect(l, WHILE);
		r->o[1] = condition(l);
		r->type = DOOP;
	} else {
		/* statement is optional */
	}
	return r;
}

static node_t *constlist(lexer_t *l) /* "const" ident "=" number {"," ident "=" number} ";" */
{
	node_t *r = new_node(l, CONSTLIST, 1);
	expect(l, IDENTIFIER);
	use(l, r);
	r->token->constant = 1;
	expect(l, EQUAL);
	expect(l, NUMBER);
	r->value = l->accepted;
	r->value->constant = 1;
	l->accepted = NULL;
	if(accept(l, COMMA))
		r->o[0] = constlist(l);
	return r;
}

static node_t *block(lexer_t *l);

static node_t *proclist(lexer_t *l) /* ident ";" block ";" procedure */
{
	node_t *r = new_node(l, PROCLIST, 3);
	expect(l, IDENTIFIER);
	use(l, r);
	r->token->procedure = 1;
	if(accept(l, COMMA)) {
		r->o[2] = varlist(l);
		r->argc = count(r->o[2], 0);
	}
	expect(l, SEMICOLON);
	r->o[0] = block(l);
	expect(l, SEMICOLON);
	if(accept(l, PROCEDURE))
		r->o[1] = proclist(l);
	return r;
}

static node_t *block(lexer_t *l)
{
	node_t *r = new_node(l, BLOCK, 4);
	if(accept(l, CONST)) { /* [ "const" constlist ] */
		r->o[0] = constlist(l);
		expect(l, SEMICOLON);
	}
	if(accept(l, VAR)) { /* [ "var" varlist ] */
		r->o[1] = varlist(l);
		expect(l, SEMICOLON);
	}
	if(accept(l, PROCEDURE)) /* "procedure" proclist */
		r->o[2] = proclist(l);
	r->o[3] = statement(l); /* statement */
	return r;
}

static node_t *program(lexer_t *l) /* block ( "." | EOF ) */
{
	node_t *r;
	assert(l);
	r = new_node(l, PROGRAM, 1);
	lexer(l);
	r->o[0] = block(l);
	if(accept(l, EOI))
		return r;
	expect(l, DOT);
	return r;
}

node_t *parse(FILE *input, int debug)
{
	lexer_t *l;
	assert(input);
	l = new_lexer(input, debug);
	l->error.jmp_buf_valid = 1;
	if(setjmp(l->error.j)) {
		free_lexer(l);
		/** @warning leaks parsed nodes */
		return NULL;
	}
	node_t *n = program(l);
	free_lexer(l);
	return n;
}

