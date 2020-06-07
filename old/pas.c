/* Program: Pascal Cross Compiler
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/vm>
 *
 * Place holder for initial Pascal cross-compiler
 *
 * This does not have to implement everything, just
 * enough to boot a Unix like kernel, be forwards compatible
 * with what I want the language to be, and be able to
 * write the initial shell and user land.
 *
 * TODO:
 * - Define grammar
 * - Implement Lexer/Parser
 * - Simple code generation
 * - Make test program/kernel
 * - Iterate compiler
 * - Make sure all code uses relative jumps/loads if possible.
 * - Write tools for packing/unpacking a simple file system disk image
 *
 * Notes:
 * 
 * - Oberon Grammar <http://oberon07.com/EBNF.txt>
 * - PL/0 Grammar <https://en.wikipedia.org/wiki/PL/0>
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

/*
digit  =   "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9".
hexDigit  =  digit | "A" | "B" | "C" | "D" | "E" | "F".
ident  =  letter {letter | digit}.
qualident  =  [ident "."] ident.
identdef = ident ["*"].
integer  =  digit {digit} | digit {hexDigit} "H".
real  =  digit {digit} "." {digit} [ScaleFactor].
ScaleFactor  =  ("E" |"D") ["+" | "-"] digit {digit}.
number  =  integer | real.
string  =  "'" {character} "'" | digit {hexdigit} "X".
ConstDeclaration  =  identdef "=" ConstExpression.
ConstExpression  =  expression.
TypeDeclaration  =  identdef "=" StrucType.
StrucType  =  ArrayType | RecordType | PointerType | ProcedureType.
type  =  qualident | StrucType.
ArrayType  =  "ARRAY" length {"," length} "OF" type.
length  =  ConstExpression.
RecordType  =  "RECORD" ["(" BaseType ")"] [FieldListSequence] "END".
BaseType  =  qualident.
FieldListSequence  =  FieldList {";" FieldList}.
FieldList  =  IdentList ":" type.
IdentList  =  identdef {"," identdef}.
PointerType  =  "POINTER" "TO" type.
ProcedureType  =  "PROCEDURE" [FormalParameters].
VariableDeclaration  =  IdentList ":" type.
expression  =  SimpleExpression [relation SimpleExpression].
relation  =  "=" | "#" | "<" | "<=" | ">" | ">=" | "IN" | "IS".
SimpleExpression  =  ["+" | "-"] term {AddOperator term}.
AddOperator  =  "+" | "-" | "OR".
term  =  factor {MulOperator factor}.
MulOperator  =  "*" | "/" | "DIV" | "MOD" | "&".
factor  =  number | string | "NIL" | "TRUE" | "FALSE" |
  set | designator [ActualParameters] | "(" expression ")" | "~" factor.
designator  =  qualident {selector}.
selector  =  "." ident | "[" ExpList "]" | "^" |  "(" qualident ")".
set  =  "{" [element {"," element}] "}".
element  =  expression [".." expression].
ExpList  =  expression {"," expression}.
ActualParameters  =  "(" [ExpList] ")" .
statement  =  [assignment | ProcedureCall | IfStatement | CaseStatement |
  WhileStatement | RepeatStatement | ForStatement].
assignment  =  designator ":=" expression.
ProcedureCall  =  designator [ActualParameters].
StatementSequence  =  statement {";" statement}.
IfStatement  =  "IF" expression "THEN" StatementSequence
  {"ELSIF" expression "THEN" StatementSequence}
  ["ELSE" StatementSequence] "END".
CaseStatement  =  "CASE" expression "OF" case {"|" case} "END".
Case  =  CaseLabelList ":"  StatementSequence.
CaseLabelList  = LabelRange {"," LabelRange}.
LabelRange  =  label [".." label].
label  =  integer | string | ident.
WhileStatement  =  "WHILE" expression "DO" StatementSequence
{"ELSIF" expression "DO" StatementSequence} "END".
RepeatStatement  =  "REPEAT" StatementSequence "UNTIL" expression.
ForStatement  =  "FOR" ident ":=" expression "TO" expression ["BY" ConstExpression]
"DO" StatementSequence "END".
ProcedureDeclaration  =  ProcedureHeading ";" ProcedureBody ident.
ProcedureHeading  =  "PROCEDURE" identdef [FormalParameters].
ProcedureBody  =  DeclarationSequence ["BEGIN" StatementSequence]
    ["RETURN" expression] "END".
DeclarationSequence  =  ["CONST" {ConstDeclaration ";"}]
  ["TYPE" {TypeDeclaration ";"}]
  ["VAR" {VariableDeclaration ";"}]
  {ProcedureDeclaration ";"}.
FormalParameters  =  "(" [FPSection {";" FPSection}] ")" [":" qualident].
FPSection  =  ["CONST" | "VAR"] ident {"," ident} ":" FormalType.
FormalType  =  ["ARRAY" "OF"] qualident.
module  =  "MODULE" ident ";" [ImportList] DeclarationSequence
  ["BEGIN" StatementSequence] "END" ident "." .
ImportList  =  "IMPORT" import {"," import} ";".
import  =  ident [":=" ident]. */

/* support code */
/* lexer/parser? {accept/expect */
/* code generator */

typedef struct {
	char *arg;   /* parsed argument */
	int error,   /* turn error reporting on/off */
	    index,   /* index into argument list */
	    option,  /* parsed option */
	    reset;   /* set to reset */
	char *place; /* internal use: scanner position */
	int  init;   /* internal use: initialized or not */
} pascal_getopt_t;   /* getopt clone; with a few modifications */

typedef struct {
	int (*getch)(void *param);
	void *param;
	int line;
} parser_t;

typedef struct {
	int type, line;
} ast_t;

static int die(const char *fmt, ...) {
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fputc('\n', stderr);
	(void)fflush(stderr);
	exit(1);
	return -1;
}

static int info(const char *fmt, ...) {
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fputc('\n', stderr);
	(void)fflush(stderr);
	return 0;
}

/* Adapted from: <https://stackoverflow.com/questions/10404448> */
static int pascal_getopt(pascal_getopt_t *opt, const int argc, char *const argv[], const char *fmt) {
	assert(opt);
	assert(fmt);
	assert(argv);
	enum { BADARG_E = ':', BADCH_E = '?' };

	if (!(opt->init)) {
		opt->place = ""; /* option letter processing */
		opt->init  = 1;
		opt->index = 1;
	}

	if (opt->reset || !*opt->place) { /* update scanning pointer */
		opt->reset = 0;
		if (opt->index >= argc || *(opt->place = argv[opt->index]) != '-') {
			opt->place = "";
			return -1;
		}
		if (opt->place[1] && *++opt->place == '-') { /* found "--" */
			opt->index++;
			opt->place = "";
			return -1;
		}
	}

	const char *oli = NULL; /* option letter list index */
	if ((opt->option = *opt->place++) == ':' || !(oli = strchr(fmt, opt->option))) { /* option letter okay? */
		 /* if the user didn't specify '-' as an option, assume it means -1.  */
		if (opt->option == '-')
			return -1;
		if (!*opt->place)
			opt->index++;
		if (opt->error && *fmt != ':')
			(void)fprintf(stderr, "illegal option -- %c\n", opt->option);
		return BADCH_E;
	}

	if (*++oli != ':') { /* don't need argument */
		opt->arg = NULL;
		if (!*opt->place)
			opt->index++;
	} else {  /* need an argument */
		if (*opt->place) { /* no white space */
			opt->arg = opt->place;
		} else if (argc <= ++opt->index) { /* no arg */
			opt->place = "";
			if (*fmt == ':')
				return BADARG_E;
			if (opt->error)
				(void)fprintf(stderr, "option requires an argument -- %c\n", opt->option);
			return BADCH_E;
		} else	{ /* white space */
			opt->arg = argv[opt->index];
		}
		opt->place = "";
		opt->index++;
	}
	return opt->option; /* dump back option letter */
}



static int parse(parser_t *p, ast_t **ast) {
	assert(p);
	assert(ast);
	*ast = NULL;
	return 0;
}

static int print(ast_t *a, FILE *o) {
	assert(a);
	assert(o);
	return 0;
}

static int get(void *param) {
	assert(param);
	return fgetc((FILE*)param);
}

static int usage(char *arg0, FILE *o) {
	assert(o);
	assert(arg0);
	static const char *help = "";
	return fprintf(o, "Usage: %s -i file -o file\n%s\n", arg0, help);
}

int main(int argc, char **argv) {
	
	pascal_getopt_t opt = { .init = 0 };
	for (int ch = 0; (ch = pascal_getopt(&opt, argc, argv, "hi:o:")) != -1;) {
		switch (ch) {
		case 'h': usage(argv[0], stdout); return 0;
		case 'i': break;
		case 'o': break;
		default:
			  info("unknown option -- %c", ch);
			  (void)usage(argv[0], stderr);
			  return 1;
		}
	}

	parser_t p = {
		.getch = get,
		.line  = 1,
		.param = stdin,
	};
	ast_t *ast = NULL;
	if (parse(&p, &ast) < 0)
		die("parsing failed");
	if (print(ast, stdout) < 0)
		die("printing failed");

	return 0;
}

