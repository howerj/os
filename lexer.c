/** @file       lexer.c
 *  @brief      PL/0 lexer
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com */

#include "pl0.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *keywords[] = 
{
	[CONST]         =  "const",
	[VAR]           =  "var",
	[PROCEDURE]     =  "procedure",
	[ASSIGN]        =  ":=",
	[CALL]          =  "call",
	[BEGIN]         =  "begin",
	[END]           =  "end",
	[IF]            =  "if",
	[THEN]          =  "then",
	[ELSE]          =  "else",
	[WHILE]         =  "while",
	[DO]            =  "do",
	[LESSEQUAL]     =  "<=",
	[GREATEREQUAL]  =  ">=",
	[ODD]           =  "odd",
	[WRITE]         =  "write",
	[READ]          =  "read",
	[AND]           =  "and",
	[OR]            =  "or",
	[XOR]           =  "xor",
	[INVERT]        =  "invert",
	NULL
};

void print_token_enum(FILE *output, token_e type)
{
	assert(output);
	if(type == NUMBER)
		fputs("number", output);
	else if(type == IDENTIFIER)
		fputs("identifier", output);
	else if(type >= 0 && type < LAST_KEY_WORD)
		fprintf(output, "key-word(%s)", keywords[type]);
	else if(type > FIRST_SINGLE_CHAR_TOKEN && type < LAST_SINGLE_CHAR_TOKEN)
		fprintf(output, "token(%c)", type);
	else if(type == EOI)
		fputs("EOF", output);
	else if(type == ERROR)
		fputs("error token", output);
	else
		fprintf(output, "invalid-token(%d)", type);
}

void print_token(FILE *output, token_t *t, unsigned depth)
{
	if(!t)
		return;
	assert(output);
	indent(output, ' ', depth);
	if(t->type == NUMBER)
		fprintf(output, "number(%d)", t->p.number);
	else if(t->type == IDENTIFIER)
		fprintf(output, "id(%s)", t->p.id);
	else
		print_token_enum(output, t->type);
	fprintf(output, " line %u\n", t->line);
}

static token_t *new_token(token_e type, unsigned line)
{
	token_t *r = allocate(sizeof(*r));
	r->type = type;
	r->line = line;
	return r;
}

void free_token(token_t *t)
{
	if(!t)
		return;
	if(t->type == IDENTIFIER)
		free(t->p.id);
	free(t);
}

lexer_t *new_lexer(FILE *input, int debug)
{
	lexer_t *r = allocate(sizeof(*r));
	r->input = input;
	r->line = 1;
	r->debug = debug;
	return r;
}

void free_lexer(lexer_t *l)
{
	assert(l);
	free_token(l->accepted);
	free(l);
}

static int next_char(lexer_t *l)
{
	assert(l);
	return fgetc(l->input);
}

static int unget_char(lexer_t *l, int c)
{
	assert(l);
	return ungetc(c, l->input);
}

/* process a comment for the lexer, out_ch is filled with the next 
   character the lexer needs, this function does not return on
   a syntax error */ 
static void comment(lexer_t *l, int *out_ch)
{
	assert(l);
	assert(out_ch);	
	for(;;) {
		int ch = next_char(l);
		if(ch == '*') {
			ch = next_char(l);
			if(ch == ')') {
				*out_ch = next_char(l);
				return;
			} else if(ch == EOF) {
				break;
			}
		} else if(ch == EOF) {
			break;
		}
	}
	syntax_error(l, "comment terminated by EOF");
}

static intptr_t number(lexer_t *l, int *c)
{
	static const char conv[] = "0123456789abcdefghijklmnopqrstuvwxzy";
	intptr_t i;
	int ch;
	assert(l);
	assert(c);
	i = 0;
	ch = *c;
	while(isdigit(ch)) {
		i = i * 10 + (ch - '0');
		ch = next_char(l);
	}
	*c = ch;
	return i;
}

void lexer(lexer_t *l)
{
	int ch;
	assert(l);
       	ch = next_char(l);
	l->token = new_token(ERROR, l->line);
again:  switch(ch) {
	case '\n': l->token->line = l->line++;
	case '\t':
	case ' ': ch = next_char(l);
		  goto again;
	case DOT: l->token->type = DOT; return;
	case EOF: l->token->type = EOI; return;
	case COMMA:    case SEMICOLON:  case RPAR:     
	case ADD:      case MUL:        case DIV:      
	case EQUAL:    case NOTEQUAL:
		l->token->type = ch;
		return;
	case SUB:
		/**@todo process negative numbers, hex and octal */
		/*ch = next_char(l);
		if(isdigit(ch)) {
			l->token->type = NUMBER;
			l->token->p.number = number(l, &ch);
			break;
		}*/
		l->token->type = ch;
		return;
	/* >, >=, <, <=, (, (* and := are special case for now */
	case LPAR:
		ch = next_char(l);
		if(ch == '*') {
			comment(l, &ch);
			goto again;
		} else {
			unget_char(l, ch);
			l->token->type = LPAR;
		}
		return;
	case LESS:
		ch = next_char(l);
		if(ch == '=') {
			l->token->type = LESSEQUAL;
		} else {
			unget_char(l, ch);
			l->token->type = LESS;
		}
		return;
	case GREATER:
		ch = next_char(l);
		if(ch == '=') {
			l->token->type = GREATEREQUAL;
		} else {
			unget_char(l, ch);
			l->token->type = GREATER;
		}
		return;
	case ':':
		ch = next_char(l);
		if(ch != '=')
			syntax_error(l, "expected '=' after ':'");
		l->token->type = ASSIGN;
		return;
	default:
		if(isdigit(ch)) {
			l->token->type = NUMBER;
			l->token->p.number = number(l, &ch);
			break;
		}
		if(isalpha(ch)) {
			unsigned i = 0, sym = 0;
			while(isalnum(ch)) {
				l->id[i++] = ch;
				ch = next_char(l);
			}
			l->id[i] = '\0';
			for(sym = CONST; keywords[sym] && strcmp(keywords[sym], l->id); sym++)
				/*do nothing*/;
			if(!keywords[sym]) {
				l->token->type = IDENTIFIER;
				l->token->p.id = duplicate(l->id);
			} else {
				l->token->type = sym;
			}
			break;
		}
		fprintf(stderr, "token: %s (%d)\n", l->id, ch);
		syntax_error(l, "invalid token");
	}
	unget_char(l, ch);
}

