/* Richard James Howe, howe.r.j.89@gmail.com, Pascal Compiler, Public Domain */
/* TODO:
 * - Grammar (C/Pascal like)
 * - Basic functionality
 * - Modules, Functions, Nested Functions, Multiple return values, Records,
 *   Asserts, Safe/Unsafe, Intrinsics, Function pointers, strings, arrays, ...
 * - Code Generation
 * - Optional Garbage Collection and Manual Memory Management
 * - Optimizations?
 * - Linker, export and load object file interfaces */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>

#define MEMORY_START  (0x0000080000000000ull)
#define IO_START      (0x0000040000000000ull)
#define MEMORY_SIZE   (1024ul * 1024ul * 1ul)
#define NELEMS(X)     (sizeof(X) / sizeof(X[0]))
#define implies(P, Q) (assert(!(P) || (Q)))
#define LEXER_DEBUG   (0)

struct ast;
typedef struct ast {
	int type, token, line;
	size_t children;
	struct ast **as;
	/* compiler data */
	uint64_t location, size;
	unsigned used, resolved, arith_type;
	/* token data */
	uint64_t d;
	char *s;
} ast_t;

struct scope;
typedef struct scope {
	ast_t *items[5]; /* NB. do not free items pointed to! */
	struct scope *parent;
} scope_t;

typedef struct {
	uint64_t start, here;
	uint64_t m[MEMORY_SIZE / sizeof (uint64_t)];
	unsigned line;
	ast_t *as, *cur;
	FILE *in, *out, *err;
	/* lexer */
	char buf[512];
	int type, prev, fail;
	char *str;
	uint64_t d;
} compile_t;

static int warn(compile_t *c, const char *fmt, ...) {
	assert(c);
	assert(fmt);
	c->fail = -1;
	if (!(c->err))
		return -1;
	va_list ap;
	const int r1 = fprintf(c->err, "%u: ", c->line);
	va_start(ap, fmt);
	const int r2 = vfprintf(c->err, fmt, ap);
	va_end(ap);
	const int r3 = fputc('\n', c->err);
	if (r1 < 0 || r2 < 0 || r3 < 0)
		return -2;
	return -1;
}

static void *allocate(const size_t sz) {
	if (sz == 0)
		return NULL;
	void *r = calloc(sz, 1);
	if (!r) {
		(void)fprintf(stderr, "Allocation of size %ld failed\n", (long)sz);
		exit(1);
	}
	return r;
}

static char *duplicate(const char *s) {
	assert(s);
	const size_t l = strlen(s) + 1;
	char *r = malloc(l);
	if (!r) {
		(void)fprintf(stderr, "string duplicate of '%s' failed\n", s);
		exit(1);
	}
	return memcpy(r, s, l);
}

static void *reallocate(void *p, size_t sz) {
	if (sz == 0) {
		free(p);
		return NULL;
	}
	void *r = realloc(p, sz);
	if (!r) {
		(void)fprintf(stderr, "unable to reallocate %p of size %ld\n", p, (long)sz);
		exit(1);
	}
	return r;
}

enum {
	END, 
	INT, STR, IDENT, LPAR, RPAR, LBRC, RBRC, SLBRC, SRBRC, ASSIGN, SEMI, DOT, COLON, COMMA,
	PLUS, MINUS, LSHIFT, RSHIFT, MUL, DIV, EQ, NEQ, GT, GTE, LT, LTE, AND, OR, XOR, INVERT, /* rotate? */
	EOI,

	IF /* <- first keyword */, ELSE, DO, WHILE, PROCEDURE, FOR, VAR, CONST, BREAK, CONTINUE, ASSERT, IMPLIES,
	TYPE, MODULE, IMPORT,
	GET, PUT, BYTES, SIZE, ADDR, TRAP, RECORD, ARRAY, POINTER, U64, S64, U8, 
	OF, ORD, TO, BY, NIL, TRUE, FALSE,
};

static const char *keywords[] = {
	"", 
	"int", "str", "id", "(", ")", "{", "}", "[", "]", ":=", ";", ".", ":", ",",
	"+", "-", "<<", ">>", "*", "/", "=", "#", ">", ">=", "<", "<=", "&", "|", "^", "~",
	"EOI",
	/* actual keywords */
	"if", "else", "do", "while", "procedure", "for", "var", "const", "break", "continue", "assert", "implies",
	"type", "module", "import",
	"get", "put", "bytes", "size", "addr", "trap", "record", "array", "pointer", "uint", "int", "byte", 
	"of", "ord", "to", "by", "nil", "true", "false",
};

static int digit(int ch, int base) {
	assert(base >= 2 && base <= 36);
	assert(ch > 0 && ch < 256);
	int r = -1;
	ch = tolower(ch);
	if (ch >= '0' && ch <= '9')
		r = ch - '0';
	if (ch >= 'a' && ch <= 'z')
		r = (ch - 'a') + 10;
	return r < base ? r : -1;
}

static int lexer(compile_t *c) {
	assert(c);
	c->prev = c->type;
	if (c->fail)
		return -1;
	int ch = 0;
again:
	ch = fgetc(c->in);
	switch (ch) {
	case '\n': c->line++; /* fall-through */
	case '\r': case ' ': case '\t': goto again;
	case ':': ch = fgetc(c->in);
		  if (ch == '=') { c->type = ASSIGN; break; }
		  c->type = COLON;
		  goto unget;
	case '.': c->type = DOT;    break;
	case EOF: c->type = EOI;    break;
	case '&': c->type = AND;    break;
	case '|': c->type = OR;     break;
	case '^': c->type = XOR;    break;
	case '~': c->type = INVERT; break;
	case '=': c->type = EQ;     break;
	case '#': c->type = NEQ;    break;
	case '-': c->type = MINUS;  break; /* TODO: Should peek ahead and change number */
	case '+': c->type = PLUS;   break;
	case '*': c->type = MUL;    break;
	case '/': c->type = DIV;    break;
	case ';': c->type = SEMI;   break;
	case ',': c->type = COMMA;  break;
	case '}': c->type = RBRC;   break;
	case '{': c->type = LBRC;   break;
	case ']': c->type = SRBRC;  break;
	case '[': c->type = SLBRC;  break;
	case ')': c->type = RPAR;   break;
	case '(': ch = fgetc(c->in);
		if (ch == '*') { /* comment */
			for (;;) {
				ch = fgetc(c->in);
				if (ch < 0)
					return warn(c, "unexpected EOF");
				if (ch == '\n')
					c->line++;
				if (ch == '*' && fgetc(c->in) == ')')
					break;
			}
			goto again;
		}
		c->type = LPAR;
		goto unget;
	case '<': ch = fgetc(c->in);
		  if (ch == '<') { c->type = LSHIFT; break; }
		  if (ch == '=') { c->type = LTE; break; }
		  c->type = LT; 
		  goto unget;
	case '>': ch = fgetc(c->in);
		  if (ch == '>') { c->type = RSHIFT; break; }
		  if (ch == '=') { c->type = GTE; break; }
		  c->type = GTE; 
		  goto unget;
	case '$': 
		ch = fgetc(c->in);
		if (!isxdigit(ch))
			return warn(c, "$ requires at least one hex digit");
		c->d = 0;
		do {
			const int n = digit(ch, 16);
			assert(n >= 0);
			const uint64_t nd = (c->d * 16ull) + n;
			if (nd < c->d)
				return warn(c, "overflow");
			c->d = nd;
			ch = fgetc(c->in);
		} while (isxdigit(ch));
		c->type = INT;
		goto unget;
	case '"': {
		const size_t max = NELEMS(c->buf) - 1;
		size_t i = 0;
		for (i = 0; i < max; i++) { /* NB: Arbitrary length strings should be added, but probably not needed */
			ch = fgetc(c->in);
			if (ch == '"')
				break;
			if (ch == EOF)
				return warn(c, "unexpected EOF");
			if (ch == '\\') {
				ch = fgetc(c->in);
				if (ch == EOF)
					return warn(c, "unexpected EOF");
				switch (ch) {
				case 'e': ch = 27; break;
				case 'a': ch = '\a'; break;
				case 't': ch = '\t'; break;
				case 'n': ch = '\n'; break;
				case 'r': ch = '\r'; break;
				case '"': ch = '\"'; break;
				case '\\': ch = '\\'; break;
				case 0:  return warn(c, "cannot encode NUL character in string");
				default: return warn(c, "unknown escape character -- %c", ch);
				}
			}
			c->buf[i] = ch;
		}
		if (i >= max)
			return warn(c, "identifier %s... too long (%d bytes)", c->buf, i);
		c->buf[i] = '\0';
		c->type = STR;
		free(c->str);
		c->str = duplicate(c->buf);
		break;
	}
	default:
		if (!isalnum(ch))
			return warn(c, "invalid character -- %c", ch);

		if (ch >= '0' && ch <= '9') {
			c->d = 0;
			do {
				const int n = digit(ch, 10);
				assert(n >= 0);
				const uint64_t nd = (c->d * 10ull) + n;
				c->d = nd;
				ch = fgetc(c->in);
			} while (isdigit(ch));
			c->type = INT;
			goto unget;
		}
		const size_t max = NELEMS(c->buf) - 1;
		size_t i = 0;
		for (i = 0; i < max; i++) {
			c->buf[i] = ch;
			ch = fgetc(c->in);
			if (!isalnum(ch))
				break;
		}
		if (i >= max)
			return warn(c, "identifier %s... too long (%d bytes)", c->buf, i);
		c->buf[i + 1] = '\0';
		for (size_t j = IF; j < NELEMS(keywords); j++) {
			if (!strcmp(c->buf, keywords[j])) {
				c->type = j;
				goto unget;
			}
		}
		c->type = IDENT;
		free(c->str);
		c->str = duplicate(c->buf);
		goto unget;
	}
	return 0;
unget:
	if (ungetc(ch, c->in) < 0) 
		return warn(c, "ungetc failed");
	return 0;
}

static int accept(compile_t *c, int sym) {
	assert(c);
	if (sym == c->type) {
		if (sym != EOI)
			if (lexer(c) < 0)
				return -1;
		return 1;
	}
	return c->fail;
}

static int peek(compile_t *c, int sym) {
	assert(c);
	if (sym == c->type)
		return 1;
	return c->fail;
}

static int any(compile_t *c, ...) {
	assert(c);
	int r = 0;
	va_list ap;
	va_start(ap, c);
	for (int sym = END; (sym = va_arg(ap, int)) != END; ) {
		if ((r = accept(c, sym)))
			break;
	}
	va_end(ap);
	return r;
}

enum {
	PROGRAM, BLOCK, STATEMENT, TYPELIST, CONSTLIST, VARLIST, PROCLIST, CONSTANT, VARIABLE, FUNCTION, 
	CONDITIONAL, LIST, CONDITION, EXPRESSION,
	UNARY_EXPRESSION, TERM, FACTOR, IDENTIFIER, NUMBER, STRING, TYPEDECL, TYPEUSAGE, IMPORTLIST,
	IF_STATEMENT, WHILE_STATEMENT, DO_STATEMENT, FOR_STATEMENT, ASSERT_STATEMENT, IMPLIES_STATEMENT, ASSIGN_STATEMENT, CALL_STATEMENT,
	DESIGNATOR, EXPRLIST, QUALIDENT, SELECTOR, ARRAY_TYPE, RECORD_TYPE, POINTER_TYPE, PROCEDURE_TYPE, FIELD,
	CONSTEXPR,
};

static char *rules[] = {
	"program", "block", "statement", "typelist", "constlist", "varlist", "proclist", "const", "var", "procedure",
	"condition", "list", "condition", "expression", "unary", "term", "factor",
	"identifier", "number", "string", "typedecl", "typeusage", "importlist",
	"if-statement", "while-statement", "do-statement", "for-statement", "assert-statement", "implies-statement", "assign-statement", "call-statement",
	"designator", "exprlist", "qualident", "selector", "array-type", "record-type", "pointer-type", "procedure-type", "field",
	"const-expression",
};



static int expect(compile_t *c, int sym) {
	assert(c);
	const int r = accept(c, sym);
	if (r)
		return r;
	/* NB. For error handling: insert expected token, mark as failure, and continue parsing,
	 * also, print syntax tree. Another way of dealing with failure would * be to keep 
	 * going until a one of ";]})" is reached. */
	return warn(c, "syntax error in '%s' -- expected '%s' and got '%s'", rules[c->cur->type], keywords[sym], keywords[c->type]);
}

static inline int use(compile_t *c, ast_t *a) { /* move ownership of string from lexer to parse tree */
	assert(c);
	assert(a);
	a->token = c->prev;
	a->s = c->str;
	a->d = c->d;
	c->str = NULL;
	return c->fail;
}

static ast_t *ast_new(compile_t *c, ast_t **ret, int type, size_t count) {
	assert(c);
	assert(ret);
	assert(*ret == NULL);
	ast_t *r = allocate(sizeof (*r));
	r->as = allocate(count * sizeof (r));
	r->type     = type;
	r->children = count;
	r->line     = c->line;
	c->cur      = r;
	*ret        = r;
	return r;
}

static void ast_free(ast_t *a) {
	if (!a)
		return;
	for (size_t i = 0; i < a->children; i++)
		ast_free(a->as[i]);
	free(a->s);
	free(a->as);
	free(a);
	return;
}

static ast_t *ast_grow(ast_t *a) {
	assert(a);
	a->as = reallocate(a->as, (a->children + 1) * sizeof (a->as[0]));
	a->as[a->children++] = NULL;
	return a;
}

static int indent(compile_t *c, const char *s, unsigned depth) {
	assert(c);
	assert(s);
	for (unsigned i = 0; i < depth; i++)
		if (fputs(s, c->err) < 0)
			return -1;
	return 0;
}

static int ast_print(compile_t *c, ast_t *a, unsigned depth) {
	assert(c);
	if (!a)
		return 0;
	if (indent(c, " ", depth) < 0)
		return -1;
	if (fprintf(c->err, "%s %s %s %ld\n", rules[a->type], keywords[a->token], a->s ? a->s : "", (long)a->d) < 0)
		return -1;
	for (size_t i = 0; i < a->children; i++)
		ast_print(c, a->as[i], depth + 1);
	return -1;
}

static int expression(compile_t *a, ast_t **r);
static int term(compile_t *a, ast_t **r);

static int unary_expression(compile_t *c, ast_t **r) { /* ["+"|"-"] term expression */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, UNARY_EXPRESSION, 2);
	if (accept(c, MINUS)) {
		a->token = c->prev;
	} else {
		(void)accept(c, PLUS);
	}
	if (term(c, &a->as[0]) < 0)
		return -1;
	return expression(c, &a->as[1]);
}

static int constexpr(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, CONSTEXPR, 1);
	return unary_expression(c, &a->as[0]);
}

static int identifier(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, IDENTIFIER, 0);
	if (expect(c, IDENT) < 0)
		return -1;
	return use(c, a);
}

static int number(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, NUMBER, 0);
	if (expect(c, INT) < 0)
		return -1;
	return use(c, a);
}

static int string(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, STRING, 0);
	if (expect(c, STR) < 0)
		return -1;
	return use(c, a);
}

static int exprlist(compile_t *c, ast_t **r) { /* expression { "," expression } */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, EXPRLIST, 1);
	if (unary_expression(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, COMMA); i++) {
		ast_grow(a);
		if (unary_expression(c, &a->as[i]) < 0)
			return -1;
	}
	return 0;
}

static int qualident(compile_t *c, ast_t **r) { /* ident [ "." ident ] */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, QUALIDENT, 1);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (accept(c, DOT)) {
		ast_grow(a);
		if (identifier(c, &a->as[1]) < 0)
			return -1;
	}
	return 0;
}

static int selector(compile_t *c, ast_t **r) { /* ["[" exprlist "]"] */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, SELECTOR, 0);
	for (size_t i = 0; accept(c, SLBRC); i++) {
		ast_grow(a);
		if (exprlist(c, &a->as[i]) < 0)
			return -1;
		if (expect(c, SRBRC) < 0)
			return -1;
	}
	/* optional */
	return 0;
}

static int designator(compile_t *c, ast_t **r) { /* qualident selector? */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, DESIGNATOR, 2);
	if (qualident(c, &a->as[0]) < 0)
		return -1;
	return selector(c, &a->as[1]);
}

static int factor(compile_t *c, ast_t **r) { /* number | string | "nil" | "true" | "false" | "(" unary-expression ")" | "~" factor */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, FACTOR, 1);
	if (any(c, NIL, TRUE, FALSE, END))
		return use(c, a);
	if (peek(c, INT))
		return number(c, &a->as[0]);
	if (peek(c, STR))
		return string(c, &a->as[0]);
	if (accept(c, INVERT)) {
		use(c, a);
		return factor(c, &a->as[0]);
	}
	if (accept(c, LPAR)) {
		if (unary_expression(c, &a->as[0]) < 0)
			return -1;
		return expect(c, RPAR);
	}
	if (designator(c, &a->as[0]) < 0)
		return -1;
	if (accept(c, LPAR)) {
		if (exprlist(c, &a->as[1]) < 0)
			return -1;
		return expect(c, RPAR);
	}
	return 0;
}

static int term(compile_t *c, ast_t **r) {  /* factor {("*"|"/") factor}. */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, TERM, 2);
	if (factor(c, &a->as[0]) < 0)
		return -1;
	if (accept(c, MUL) || accept(c, DIV)) {
		a->token = c->prev;
		return factor(c, &a->as[1]);
	}
	return 0;
}

static int expression(compile_t *c, ast_t **r) {  /* { ("+"|"-"...) term}. */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, EXPRESSION, 1);
	if (any(c, PLUS, MINUS, AND, OR, XOR, LSHIFT, RSHIFT, END)) {
		a->token = c->prev;
		return term(c, &a->as[0]);
	}
	return 0;
}

static int condition(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, CONDITION, 2);
	if (unary_expression(c, &a->as[0]) < 0)
		return -1;
	if (any(c, EQ, NEQ, GTE, GT, LTE, LT, END)) {
		a->token = c->prev;
		return unary_expression(c, &a->as[1]);
	}
	return warn(c, "expected conditional");
}

static int statement(compile_t *c, ast_t **r);

static int list(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, LIST, 1);
	if (statement(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, SEMI); i++) {
		ast_grow(a);
		if (statement(c, &a->as[i]) < 0)
			return -1;
	}
	return 0;
}

static int typeusage(compile_t *c, ast_t **r);

static int field(compile_t *c, ast_t **r) { /* ident ":" type */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, RECORD_TYPE, 2);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (expect(c, COLON) < 0)
		return -1;
	return typeusage(c, &a->as[1]);
}

static int record_type(compile_t *c, ast_t **r) { /* "RECORD" { fieldlist } */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, RECORD_TYPE, 1);
	if (expect(c, LBRC) < 0)
		return -1;
	if (field(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, SEMI); i++) {
		ast_grow(a);
		if (field(c, &a->as[i]) < 0)
			return -1;
	}
	if (expect(c, RBRC) < 0)
		return -1;
	return 0;
}

static int array_type(compile_t *c, ast_t **r) { /* "ARRAY" constexpr { "," constexpr } "OF" "TYPE" */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, ARRAY_TYPE, 2);
	if (constexpr(c, &a->as[0]) < 0)
		return -1;
	size_t i = 1;
	for (; accept(c, COMMA); i++) {
		ast_grow(a);
		if (constexpr(c, &a->as[i]) < 0)
			return -1;
	}
	if (expect(c, OF) < 0)
		return -1;
	return typeusage(c, &a->as[i]);
}

static int pointer_type(compile_t *c, ast_t **r) { /* "POINTER" "TO" type */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, POINTER_TYPE, 1);
	if (expect(c, TO) < 0)
		return -1;
	return typeusage(c, &a->as[0]);
}

static int varlist(compile_t *c, ast_t **r);

static int procedure_type(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, PROCEDURE_TYPE, 2);
	if (expect(c, LPAR) < 0)
		return -1;
	if (peek(c, IDENT)) {
		if (varlist(c, &a->as[0]) < 0)
			return -1;
	}
	if (expect(c, RPAR) < 0)
		return -1;
	if (accept(c, COLON))
		if (typeusage(c, &a->as[1]) < 0)
			return -1;
	return 0;
}

static int typeusage(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, TYPEUSAGE, 0);
	if (accept(c, S64))
		return use(c, a);
	if (accept(c, U64))
		return use(c, a);
	if (accept(c, U8))
		return use(c, a);
	ast_grow(a);
	if (accept(c, POINTER))
		return pointer_type(c, &a->as[0]);
	if (accept(c, RECORD))
		return record_type(c, &a->as[0]);
	if (accept(c, PROCEDURE))
		return procedure_type(c, &a->as[0]);
	if (accept(c, ARRAY))
		return array_type(c, &a->as[0]);
	return qualident(c, &a->as[0]);
}

static int typedecl(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, TYPEDECL, 2);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (expect(c, EQ) < 0)
		return -1;
	return typeusage(c, &a->as[1]);
}

static int typelist(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, TYPELIST, 1);
	if (typedecl(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, COMMA); i++) {
		ast_grow(a);
		if (typedecl(c, &a->as[i]) < 0)
			return -1;
	}
	return 0;
}

static int variable(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, VARIABLE, 2);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (accept(c, COLON))
		return typeusage(c, &a->as[1]);
	return 0;
}

static int varlist(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, LIST, 1);
	if (variable(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, COMMA); i++) {
		ast_grow(a);
		if (variable(c, &a->as[i]) < 0)
			return -1;
	}
	return 0;
}

static int constant(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, CONSTANT, 3);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (accept(c, COLON))
		if (typeusage(c, &a->as[1]) < 0)
			return -1;
	if (expect(c, EQ) < 0)
		return -1;
	return constexpr(c, &a->as[2]);
}

static int constlist(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, CONSTLIST, 1);
	if (constant(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, COMMA); i++) {
		ast_grow(a);
		if (constant(c, &a->as[i]) < 0)
			return -1;
	}
	return 0;
}

static int block(compile_t *c, ast_t **r);

static int function(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, FUNCTION, 3);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (procedure_type(c, &a->as[1]) < 0)
		return -1;
	if (expect(c, LBRC) < 0)
		return -1;
	if (block(c, &a->as[2]) < 0)
		return -1;
	return expect(c, RBRC);
}


static int proclist(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, PROCLIST, 1);
	if (function(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, PROCEDURE); i++) {
		ast_grow(a);
		if (function(c, &a->as[i]) < 0)
			return -1;
	}
	return 0;
}

static int assign_statement(compile_t *c, ast_t **r, ast_t *first) {
	assert(c);
	assert(r);
	assert(first);
	ast_t *a = ast_new(c, r, ASSIGN_STATEMENT, 2);
	a->as[0] = first;
	if (expect(c, ASSIGN) < 0)
		return -1;
	return unary_expression(c, &a->as[1]);
}

static int call_statement(compile_t *c, ast_t **r, ast_t *first) {
	assert(c);
	assert(r);
	assert(first);
	ast_t *a = ast_new(c, r, CALL_STATEMENT, 2);
	a->as[0] = first;
	if (expect(c, LPAR) < 0)
		return -1;
	if (exprlist(c, &a->as[1]) < 0)
		return -1;
	return expect(c, RPAR);
}

static int assert_statement(compile_t *c, ast_t **r) { /* TODO: make this an intrinsic, remove from grammar */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, ASSERT_STATEMENT, 1);
	return condition(c, &a->as[0]);
}

static int implies_statement(compile_t *c, ast_t **r) { /* TODO: make this an intrinsic, remove from grammar */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, IMPLIES_STATEMENT, 2);
	if (condition(c, &a->as[0]) < 0)
		return -1;
	if (expect(c, COMMA) < 0)
		return -1;
	return condition(c, &a->as[1]);
}

static int do_statement(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, DO_STATEMENT, 2);
	if (statement(c, &a->as[0]) < 0)
		return -1;
	if (expect(c, WHILE) < 0)
		return -1;
	if (condition(c, &a->as[1]) < 0)
		return -1;
	return 0;
}

static int while_statement(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, WHILE_STATEMENT, 2);
	if (condition(c, &a->as[0]) < 0)
		return -1;
	/* need "do"? */
	return statement(c, &a->as[1]);
}

static int if_statement(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, IF_STATEMENT, 2);
	if (condition(c, &a->as[0]) < 0)
		return -1;
	if (statement(c, &a->as[1]) < 0)
		return -1;
	for (size_t i = 2; accept(c, ELSE); i++) {
		ast_grow(a);
		if (accept(c, IF)) {
			ast_grow(a);
			if (condition(c, &a->as[i + 0]) < 0)
				return -1;
			if (statement(c, &a->as[i + 1]) < 0)
				return -1;
			i++;
			continue;
		}
		if (statement(c, &a->as[i]) < 0)
			return -1;
		break;
	}
	return 0;
}

static int for_statement(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, FOR_STATEMENT, 3);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (expect(c, ASSIGN) < 0)
		return -1;
	if (unary_expression(c, &a->as[1]) < 0)
		return -1;
	if (expect(c, TO) < 0)
		return -1;
	if (unary_expression(c, &a->as[2]) < 0)
		return -1;
	if (accept(c, BY)) {
		ast_grow(a);
		if (constexpr(c, &a->as[3]) < 0)
			return -1;
	}
	return 0;
}

static int statement(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, STATEMENT, 1);
	if (peek(c, IDENT)) {
		ast_t *first = NULL;
		if (designator(c, &first) < 0) {
			a->as[0] = first; /* make sure we don't leak */
			return -1;
		}
		if (peek(c, ASSIGN))
			return assign_statement(c, &a->as[0], first);
		return call_statement(c, &a->as[0], first);
	} else if (accept(c, LBRC))  { if (list(c, &a->as[0]) < 0) return -1; return expect(c, RBRC); } 
	else if (accept(c, IF))      { return if_statement(c, &a->as[0]); }
	else if (accept(c, FOR))     { return for_statement(c, &a->as[0]); }
	else if (accept(c, WHILE))   { return while_statement(c, &a->as[0]); }
	else if (accept(c, DO))      { return do_statement(c, &a->as[0]); }
	else if (accept(c, ASSERT))  { return assert_statement(c, &a->as[0]); }
	else if (accept(c, IMPLIES)) { return implies_statement(c, &a->as[0]); } 
	else { /* statement is optional */ }
	return 0;
}

static int block(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, BLOCK, 5);
	*r = a;
	if (accept(c, TYPE)) {
		if (typelist(c, &a->as[0]) < 0)
			return -1;
		if (expect(c, SEMI) < 0)
			return -1;
	}
	if (accept(c, CONST)) {
		if (constlist(c, &a->as[1]) < 0)
			return -1;
		if (expect(c, SEMI) < 0)
			return -1;
	}
	if (accept(c, VAR)) {
		if (varlist(c, &a->as[2]) < 0)
			return -1;
		if (expect(c, SEMI) < 0)
			return -1;
	}
	if (accept(c, PROCEDURE))
		if (proclist(c, &a->as[3]) < 0)
			return -1;
	return list(c, &a->as[4]);
}

static int importlist(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, IMPORTLIST, 1);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	for (size_t i = 1; accept(c, COMMA); i++) {
		ast_grow(a);
		if (identifier(c, &a->as[i]) < 0)
			return -1;
	}
	return 0;
}

static int program(compile_t *c, ast_t **r) { /* block ("." | EOI) */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, PROGRAM, 3);
	if (expect(c, MODULE) < 0)
		return -1;
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (expect(c, SEMI) < 0)
		return -1;
	if (accept(c, IMPORT)) {
		if (importlist(c, &a->as[1]) < 0)
			return -1;
		if (expect(c, SEMI) < 0)
			return -1;
	}
	if (block(c, &a->as[2]) < 0)
		return -1;
	if (accept(c, DOT))
		return 0;
	return expect(c, EOI);
}

static ast_t *parse(compile_t *c) {
	assert(c);
	ast_t *r = NULL;
	if (program(c, &r) < 0) {
		ast_free(r);
		return NULL;
	}
	assert(r);
	return r;
}

enum { 
	I_FLG_JMP   = 0x8000ull, I_FLG_REL   = 0x4000ull, I_FLG_CAL = 0x2000ull, I_FLG_PSH = 0x2000ull, I_FLG_EXT = 0x1000ull,
	I_FLG_V     = 0x0800ull, I_FLG_C     = 0x0400ull, I_FLG_Z   = 0x0200ull, I_FLG_N   = 0x0100ull,
	I_FLG_POP_B = 0x0080ull, I_FLG_POP_A = 0x0040ull, 
};

static void fix(compile_t *c, uint64_t hole, uint64_t patch) {
	assert(c);
	const uint64_t i = (hole - c->start) / sizeof (uint64_t);
	c->m[i] = patch;
}

static uint64_t jump(compile_t *c, uint64_t flags) {
	assert(c);
	const uint64_t h = c->here;
	const uint64_t i = (c->here - c->start) / sizeof (uint64_t);
	c->m[i] = (0x8000ull << 48) | flags;
	c->here += sizeof (uint64_t);
	return h;
}

static int code(compile_t *c, ast_t *a, scope_t *s) {
	assert(c);
	assert(a);
	assert(s);
	uint64_t hole1 = 0/*, hole2 = 0*/;

	switch (a->type) {
	case PROGRAM:           break;
	case IMPORTLIST:        break; /* TODO: must call to initialization code for each module */
	case BLOCK: {
		scope_t ns = { 
			.parent = s,
			.items = {
				s->items[0],
				a->as[0],
				a->as[1],
				a->as[2],
				a->as[3],
			},
		};
		if (code(c, a->as[0], &ns) < 0) /* types */
			return -1;
		if (code(c, a->as[0], &ns) < 0) /* constants */
			return -1;
		if (code(c, a->as[0], &ns) < 0) /* variables */
			return -1;
		if (!(s->parent))
			hole1 = jump(c, 1);
		if (code(c, a->as[4], &ns) < 0) /* procedures */
			return -1;
		if (!(s->parent))
			fix(c, hole1, c->here - c->start);
		if (code(c, a->as[4], &ns) < 0) /* statement */
			return -1;
		/* TODO: emit return, and generate run only once code */
		break;
	}
	case STATEMENT:
		break;
	case TYPELIST:          break;
	case CONSTLIST:         break; /* evaluate expressions */
	case VARLIST:           break; /* calculate stack offsets for each variable */
	case PROCLIST:          break; /* generate code for each procedure */
	case CONSTANT:          break;
	case VARIABLE:          break;
	case FUNCTION:          break;
	case CONDITIONAL:       break;
	case LIST:              break;
	case CONDITION:         break;
	case EXPRESSION:        break;
	case UNARY_EXPRESSION:  break; /* TODO: evaluation of constant expressions, and partial evaluation, this should allow some dead code elimination */
	case TERM:              break;
	case FACTOR:            break;
	case IDENTIFIER:        break;
	case NUMBER:            break;
	case STRING:            break;
	case TYPEDECL:          break;
	case TYPEUSAGE:         break;
	case IF_STATEMENT:      break;
	case WHILE_STATEMENT:   break;
	case DO_STATEMENT:      break;
	case ASSERT_STATEMENT:  break;
	case IMPLIES_STATEMENT: break;
	case ASSIGN_STATEMENT:  break;
	case CALL_STATEMENT:    break;
	case DESIGNATOR:        break;
	case EXPRLIST:          break;
	case QUALIDENT:         break;
	case SELECTOR:          break;
	case ARRAY_TYPE:        break;
	case RECORD_TYPE:       break;
	case POINTER_TYPE:      break;
	case PROCEDURE_TYPE:    break;
	case FIELD:             break;
	case CONSTEXPR:         break;
	default: return -1;
	}

	return 0;
}

static int save(compile_t *c) {
	assert(c);
	const size_t elements = (c->start - MEMORY_START) / sizeof (uint64_t);
	assert(elements < NELEMS(c->m));
	for (size_t i = 0; i < elements; i++)
		if (fprintf(c->out, "%16"PRIx64"\n", c->m[i]) < 0)
			return warn(c, "failed to save");
	return 0;
}

static int lexer_debug(compile_t *c) {
	assert(c);
	for (;c->type != EOI;) {
		if (lexer(c) < 0)
			goto fail;
		if (fprintf(stdout, "type=%d str=%s n=%ld\n", c->type, c->str ? c->str : "(nil)", (long)c->d) < 0)
			goto fail;
	}
	free(c->str);
	c->str = NULL;
	return 0;
fail:
	free(c->str);
	c->str = NULL;
	return -1;
}

static int compile(compile_t *c) {
	assert(c);
	assert(c->in);
	assert(c->out);
	if (LEXER_DEBUG)
		return lexer_debug(c);
	if (lexer(c) < 0)
		return -1;
	if (!(c->as = parse(c)))
		return -1;
	if (ast_print(c, c->as, 0) < 0)
		return -1;
	scope_t s = { .parent = NULL };
	const int r = code(c, c->as, &s);
	ast_free(c->as);
	c->as = NULL;
	if (r < 0)
		return -1;
	return save(c);
}

static FILE *fopen_or_die(const char *name, const char *mode) {
	assert(name && mode);
	FILE *r = fopen(name, mode);
	if (!r) {
		(void)fprintf(stderr, "Unable to open file %s in mode %s: %s", name, mode, strerror(errno));
		exit(1);
	}
	return r;
}

/* TODO: Command line arguments, processing location list for linker */
int main(int argc, char **argv) {
	int r = 0;
	compile_t c = { .in = NULL, };
	if (argc != 3) {
		(void)fprintf(stderr, "Usage: %s in.p out.hex\n", argv[0]);
		return 1;
	}
	c.in = fopen_or_die(argv[1], "rb");
	c.out = fopen_or_die(argv[2], "wb");
	c.err = stderr;
	if (compile(&c) < 0)
		r = 1;
	if (fclose(c.in) < 0) 
		r = 1;
	if (fclose(c.out) < 0)
		r = 1;
	return r;
}

