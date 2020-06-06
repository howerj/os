/** @file       pl0.h
 *  @brief      PL/0 header file
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com */

#ifndef PL0_H
#define PL0_H

#include <stdio.h>
#include <setjmp.h>
#include <stdint.h>

#define MAX_ID_LENGTH (256u)
#define MAX_CORE      (1024u)
#define MAX_STACK     (512u)
#define VERSION       (1)

/********************************* lexer ***********************************/

typedef enum 
{
	/*these need to go first, in this order*/
	CONST,		/* const */
	VAR,		/* var */
	PROCEDURE,	/* procedure */
	ASSIGN,         /* := */
	CALL,		/* call */
	BEGIN,          /* begin */
	END,		/* end */
	IF,		/* if */
	THEN,		/* then */
	ELSE,           /* else */
	WHILE,		/* while */
	DO,		/* do */
	LESSEQUAL,      /* <= */
	GREATEREQUAL,   /* >= */
	ODD,            /* odd */
	WRITE,          /* write */
	READ,           /* read */
	AND,
	OR,
	XOR,
	INVERT,
	LAST_KEY_WORD,  /* not an actual token */

	ERROR,          /* invalid token, not an actual token */
	IDENTIFIER,     /* [a-Z_][0-9a-Z]* */
	NUMBER,         /* [0-9]+ */

	FIRST_SINGLE_CHAR_TOKEN, /* marker, not a token */
	/* @warning these need to be in ASCII order */
	NOTEQUAL     =  '#',
	LPAR         =  '(',
	RPAR         =  ')',
	MUL          =  '*',
	SUB          =  '-',
	DIV          =  '/',
	ADD          =  '+',
	COMMA        =  ',',
	DOT          =  '.',
	SEMICOLON    =  ';',
	LESS         =  '<',
	EQUAL        =  '=',
	GREATER      =  '>',
	LAST_SINGLE_CHAR_TOKEN, /* marker, not a token */
	EOI          =  EOF
} token_e;

typedef struct {
	int error;
	int jmp_buf_valid;
	jmp_buf j;
} error_t;

typedef struct {
	union {
		char *id;
		int number;
	} p;
	token_e  type;
	unsigned level;    /* frame variables are accessible in */
	unsigned location; /* location in code (for variables, functions) */
	unsigned line;     /* line token encountered */
	unsigned global    :1,  /* global data */
		 constant  :1,  /* constant variable */
		 procedure :1,  /* is a procedure */
		 located   :1;  /* has this function been emitted as code already? */
} token_t;

typedef struct {
	FILE *input;
	unsigned line;
	int c;
	char id[MAX_ID_LENGTH];
	int number;
	int debug;
	token_t *token;
	token_t *accepted;
	error_t error;
} lexer_t;

/********************************* parser **********************************/

#define XMACRO_PARSE\
	X(PROGRAM,           "program")\
	X(BLOCK,             "block")\
	X(STATEMENT,         "statement")\
	X(CONSTLIST,         "constants")\
	X(VARLIST,           "variables")\
	X(PROCLIST,          "procedures")\
	X(ASSIGNMENT,        "assignment")\
	X(INVOKE,            "invocation")\
	X(OUTPUT,            "output")\
	X(INPUT,             "input")\
	X(CONDITIONAL,       "conditional")\
	X(WHILST,            "whilst")\
	X(DOOP,              "do")\
	X(LIST,              "list")\
	X(CONDITION,         "condition")\
	X(EXPRESSION,        "expression")\
	X(UNARY_EXPRESSION,  "unary-expression")\
	X(UNARY_EXPR_LIST,   "unary-expression-list")\
	X(TERM,              "term")\
	X(FACTOR,            "factor")\
	X(LAST_PARSE,        "INVALID")

typedef enum {
#define X(ENUM, NAME) ENUM,
	XMACRO_PARSE
#undef X
} parse_e;

typedef struct node_t  {
	unsigned type   :8, /* of parse_e type */
		 length :8;
	size_t argc; /* count of arguments for functions */
	token_t *token, *value;
	struct node_t *o[];
} node_t;

/**************************** code generation ******************************/

typedef struct {
	intptr_t location;
	intptr_t value;
} patch_t;

typedef struct {
	unsigned here;
	unsigned globals;
	unsigned size;
	error_t error;
	node_t* root;
	node_t** debug;
	size_t patchcount;
	patch_t *patches;
	intptr_t m[];
} code_t;

typedef struct scope_t {
	node_t *constants;
	node_t *variables;
	node_t *functions;
	node_t *current;
	size_t allocated;
	struct scope_t *parent;
} scope_t;

#define XMACRO_INSTRUCTION\
	X(INOP,     "nop")\
	X(ILOAD,    "load")\
	X(ISTORE,   "store")\
	X(IVLOAD,   "vload")\
	X(IVSTORE,  "vstore")\
	X(ICALL,    "call")\
	X(IRETURN,  "return")\
	X(IJMP,     "jmp")\
	X(IJZ,      "jz")\
	X(IJNZ,     "jnz")\
	X(IADD,     "+")\
	X(ISUB,     "-")\
	X(IMUL,     "*")\
	X(IDIV,     "/")\
	X(ILTE,     "<=")\
	X(IGTE,     ">=")\
	X(ILT,      "<")\
	X(IGT,      ">")\
	X(IEQ,      "=")\
	X(INEQ,     "#")\
	X(IAND,     "and")\
	X(IOR,      "or")\
	X(IXOR,     "xor")\
	X(IINVERT,  "invert")\
	X(INEGATE,  "negate")\
	X(IODD,     "odd")\
	X(IPUSH,    "push")\
	X(IPOP,     "pop")\
	X(IWRITE,   "write")\
	X(IREAD,    "read")\
	X(IHALT,    "halt")

typedef enum {
#define X(ENUM, NAME) ENUM,
	XMACRO_INSTRUCTION
#undef X
} instruction;

/**************************** virtual machine ******************************/

typedef enum {
	VM_ERROR_OK         =  0,
	VM_ERROR_READ       = -1,
	VM_ERROR_WRITE      = -2,
	VM_ERROR_DIV0       = -3,
	VM_ERROR_ILLEGAL_OP = -4,
} vm_errors;

/**************************** virtual machine ******************************/
/************************* function prototypes *****************************/

void *allocate(size_t sz);
char *duplicate(const char *str);
void ethrow(error_t *e);
void indent(FILE *output, char c, unsigned i);

int instruction_dump(code_t *c, FILE *output, int nprint, unsigned i);
void dump(code_t *c, FILE *output, int nprint);
int vm(code_t *c, FILE *input, FILE *output, int debug);

lexer_t *new_lexer(FILE *input, int debug);
void free_token(token_t *t);
void free_lexer(lexer_t *l);
void lexer(lexer_t *l);
void print_token(FILE *output, token_t *t, unsigned depth);
void print_token_enum(FILE *output, token_e type);

void print_node(FILE *output, node_t *n, int shallow, unsigned depth);
node_t *parse(FILE *input, int debug);
void free_node(node_t *n);

code_t *code(node_t *n, size_t size, int debug);
void export(node_t *n, FILE *output);
void free_code(code_t *c);

int _syntax_error(lexer_t *l, const char *file, const char *func, unsigned line, const char *msg);
#define syntax_error(L, MSG) _syntax_error((L), __FILE__, __func__, __LINE__, (MSG))

#endif
