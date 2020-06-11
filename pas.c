/* Pascal/Oberon compiler for a virtual machine
 * Author: Richard James Howe
 * License: MIT
 * Repository: https//github.com/howerj/vm
 *
 * See: <https://en.wikipedia.org/wiki/Recursive_descent_parser>
 * And: <https://en.wikipedia.org/wiki/PL/0>
 *
 * TODO: Start by modifying a PL/0 or Oberon grammar, add multiple returns,
 * some compile intrinsics, modules, function arguments, nested procedures,
 * records, arrays and strings. Some basic optimizations should probably done
 * one the code, see <https://en.wikipedia.org/wiki/Static_single_assignment_form>. */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_START  (0x0000080000000000ull)
#define IO_START      (0x0000040000000000ull)
#define MEMORY_SIZE   (1024ull * 1024ull)
#define NELEMS(X)     (sizeof(X) / sizeof(X[0]))
#define implies(P, Q) (assert(!(P) || (Q)))

struct ast;
struct record;
typedef struct ast ast_t;
typedef struct record record_t;

struct ast {
	int type, token, line;
	size_t children;
	ast_t **as;
	/* compiler data */
	uint64_t location;
	int used, resolved;
	/* token data */
	uint64_t d;
	char *s;
};

typedef struct {
	uint64_t start, here;
	uint64_t m[MEMORY_SIZE];
	unsigned line;
	ast_t *as;
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
		fprintf(stderr, "allocation of size %ld failed\n", (long)sz);
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
	INT, STR, IDENT, LPAR, RPAR, LBRC, RBRC, ASSIGN, SEMI, DOT, COLON, COMMA,
	PLUS, MINUS, LSHIFT, RSHIFT, MUL, DIV, EQ, NEQ, GT, GTE, LT, LTE, AND, OR, XOR, INVERT, /* rotate? */
	EOI,

	IF /* <- first keyword */, ELSE, DO, WHILE, PROCEDURE, FOR, VAR, CONST, BREAK, CONTINUE, ASSERT, IMPLIES,
	TYPE, MODULE, IMPORT,
	GET, PUT, BYTES, SIZE, ADDR,
};

static const char *keywords[] = {
	"", 
	"int", "str", "id", "(", ")", "{", "}", ":=", ";", ".", ":", ",",
	"+", "-", "<<", ">>", "*", "/", "=", "#", ">", ">=", "<", "<=", "&", "|", "^", "~",
	"EOI",
	/* actual keywords */
	"if", "else", "do", "while", "procedure", "for", "var", "const", "break", "continue", "assert", "implies",
	"type", "module", "import",
	"get", "put", "bytes", "size", "addr", /* trap, integer, other instrinsics, built-in types */
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
	case '-': c->type = MINUS;  break;
	case '+': c->type = PLUS;   break;
	case '*': c->type = MUL;    break;
	case '/': c->type = DIV;    break;
	case ';': c->type = SEMI;   break;
	case ',': c->type = COMMA;  break;
	case '}': c->type = RBRC;   break;
	case '{': c->type = LBRC;   break;
	case ')': c->type = RPAR; break;
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
		for (i = 0; i < max; i++) { /* TODO: Arbitrary length strings */
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
		for (size_t i = IF; i < NELEMS(keywords); i++) {
			if (!strcmp(c->buf, keywords[i])) {
				c->type = i;
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
		if (sym != EOI && sym != DOT)
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

static int expect(compile_t *c, int sym) {
	assert(c);
	const int r = accept(c, sym);
	if (r)
		return r;
	/* TODO: insert expected token, mark as failure, and continue parsing,
	 * also, print syntax tree? Another way of dealing with failure would
	 * be to keep going until a one of ";]})" is reached. */
	return warn(c, "syntax error (%d,%d) -- expected '%s' and got '%s'", sym, c->type, keywords[sym], keywords[c->type]);
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

enum {
	PROGRAM, BLOCK, STATEMENT, TYPELIST, CONSTLIST, VARLIST, PROCLIST, CONSTANT, VARIABLE, FUNCTION, 
	CONDITIONAL, LIST, CONDITION, EXPRESSION,
	UNARY_EXPRESSION, TERM, FACTOR, IDENTIFIER, NUMBER, STRING, TYPEDECL, TYPEUSAGE, IMPORTLIST,
};

static char *rules[] = {
	"program", "block", "statement", "typelist", "constlist", "varlist", "proclist", "const", "var", "procedure",
	"condition", "list", "condition", "expression", "unary", "term", "factor",
	"identifier", "number", "string", "typedecl", "typeusage", "importlist",
};

static ast_t *ast_new(compile_t *c, ast_t **ret, int type, size_t count) {
	assert(c);
	assert(ret);
	assert(*ret == NULL);
	ast_t *r = allocate(sizeof (*r));
	r->as = allocate(count * sizeof (r));
	r->type     = type;
	r->children = count;
	r->line     = c->line;
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
		a->type = c->prev;
	} else {
		(void)accept(c, PLUS);
	}
	if (term(c, &a->as[0]) < 0)
		return -1;
	return expression(c, &a->as[1]);
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

static int factor(compile_t *c, ast_t **r) { /* ident | number | string | "(" unary-expression ")" */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, FACTOR, 1);
	if (peek(c, IDENT))
		return identifier(c, &a->as[0]);
	if (peek(c, INT))
		return number(c, &a->as[0]);
	if (peek(c, STR))
		return string(c, &a->as[0]);
	if (expect(c, LPAR) < 0)
		return -1;
	if (unary_expression(c, &a->as[0]) < 0)
		return -1;
	return expect(c, RPAR);
}

static int term(compile_t *c, ast_t **r) {  /* factor {("*"|"/") factor}. */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, TERM, 2);
	if (factor(c, &a->as[0]) < 0)
		return -1;
	if (accept(c, MUL) || accept(c, DIV)) {
		a->type = c->prev;
		return factor(c, &a->as[1]);
	}
	return 0;
}

static int expression(compile_t *c, ast_t **r) {  /* { ("+"|"-"...) term}. */
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, EXPRESSION, 1);
	if (any(c, PLUS, MINUS, AND, OR, XOR, END)) {
		a->type = c->prev;
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
		a->type = c->prev;
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

static int typeusage(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, TYPEUSAGE, 1);
	return identifier(c, &a->as[0]);
}

static int typedecl(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, TYPEDECL, 1);
	return identifier(c, &a->as[0]);
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

static int varlist(compile_t *c, ast_t **r) { /* TODO: make varlist entirely optional? */
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
	return unary_expression(c, &a->as[2]);
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
	ast_t *a = ast_new(c, r, FUNCTION, 4);
	if (identifier(c, &a->as[0]) < 0)
		return -1;
	if (accept(c, COLON)) {
		if (typeusage(c, &a->as[1]) < 0)
			return -1;
	}
	if (expect(c, LPAR) < 0)
		return -1;
	if (peek(c, IDENT)) {
		if (varlist(c, &a->as[2]) < 0)
			return -1;
	}
	if (expect(c, RPAR) < 0)
		return -1;
	if (expect(c, LBRC) < 0)
		return -1;
	if (block(c, &a->as[3]) < 0)
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

static int statement(compile_t *c, ast_t **r) {
	assert(c);
	assert(r);
	ast_t *a = ast_new(c, r, STATEMENT, 1);
	a->token = -1;
	if (peek(c, IDENT)) {
		ast_grow(a);
		if (identifier(c, &a->as[0]) < 0)
			return -1;
		if (accept(c, ASSIGN)) { /* TODO: handle multiple return values */
			a->token = c->prev;
			return unary_expression(c, &a->as[1]);
		}
		/* TODO: Handle bare function calls */
		return 0;
	} else if (accept(c, LBRC)) { /* '{' list '}' */
		if (list(c, &a->as[0]) < 0)
			return -1;
		return expect(c, RBRC);
	} else if (accept(c, IF)) {
		a->token = c->prev;
		ast_grow(a);
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
	} else if (accept(c, WHILE)) {
		a->token = c->prev;
		ast_grow(a);
		if (condition(c, &a->as[0]) < 0)
			return -1;
		/* need "do"? */
		return statement(c, &a->as[1]);
	} else if (accept(c, DO)) {
		a->token = c->prev;
		ast_grow(a);
		if (statement(c, &a->as[0]) < 0)
			return -1;
		if (expect(c, WHILE) < 0)
			return -1;
		if (condition(c, &a->as[0]) < 0)
			return -1;
		return 0;
	} else if (accept(c, ASSERT)) {
		a->token = c->prev;
		return condition(c, &a->as[0]);
	} else if (accept(c, IMPLIES)) {
		a->token = c->prev;
		ast_grow(a);
		if (condition(c, &a->as[0]) < 0)
			return -1;
		if (expect(c, COMMA) < 0)
			return -1;
		return condition(c, &a->as[1]);
	} else {
		/* statement is optional */
	}
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
	return statement(c, &a->as[4]);
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

struct scope;
typedef struct scope scope_t;

enum {
	IA, IB, IINVERT, IAND, IOR, IXOR, IADDC, IADD, 
	ISUBC, ISUB, ILSHIFT, IRSHIFT, IMUL, IDIV, IPCLOAD,
	IPCSTORE, ISPLOAD, ISPSTORE, IFLAGSLOAD, IFLAGSSTORE, ITRAP,
	ITRAPLSET, ILOAD, ISTORE, ILOADB, ISTOREB, ITLBFLSHS,
	ITLBFLSHA, ITLBINSTALL,
};

struct scope { /* types, constants, variables, functions (in module scope and from import list) */
	ast_t *items[5]; /* NB. do not free items pointed to! */
	scope_t *parent;
};

static ast_t *scope_find(scope_t *s, int *is_local, int type) {
	assert(s);
	if (is_local)
		*is_local = 1;
	for (scope_t *p = s; p; p = p->parent) {
		for (size_t i = 0; i < NELEMS(p->items); i++) {
			ast_t *a = p->items[i];
			if (!a)
				continue;
			if (a->type == type)
				return a;
			/* strcmp on identifier? */
		}
		*is_local = 0;
	}
	*is_local = 0;
	return NULL;
}

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
		break;
	}
	case STATEMENT:
		switch (a->token) { /* TODO: Give each of these their own production rules...*/
		case IF:
		case ASSIGN:
		case DO:
		case WHILE:
		case ASSERT:
		case IMPLIES:
			break;
		case -1: /* list of statements */
			break;
		default:
			return -1;
		}
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
	case UNARY_EXPRESSION:  break;
	case TERM:              break;
	case FACTOR:            break;
	case IDENTIFIER:        break;
	case NUMBER:            break;
	case STRING:            break;
	case TYPEDECL:          break;
	case TYPEUSAGE:         break;
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

static int compile(compile_t *c) {
	assert(c);
	assert(c->in);
	assert(c->out);
	if (lexer(c) < 0)
		return -1;
	if (!(c->as = parse(c)))
		return -1;
	ast_print(c, c->as, 0);
	scope_t s = { .parent = NULL };
	const int r = code(c, c->as, &s);
	ast_free(c->as);
	c->as = NULL;
	if (r < 0)
		return -1;
	return save(c);
}

static FILE *fopen_or_die(const char *file, const char *mode) {
	assert(file);
	assert(mode);
	errno  = 0;
	FILE *f = fopen(file, mode);
	if (!f) {
		(void)fprintf(stderr, "Could not open file '%s' in mode '%s': %s\n", file, mode, strerror(errno));
		exit(1);
	}
	return f;
}

int main(int argc, char **argv) {
	static compile_t c;
	memset(&c, 0, sizeof c);
	c.in    = stdin;
	c.out   = stdout;
	c.err   = stderr;
	c.start = MEMORY_START;
	c.here  = MEMORY_START;
	int r = 0;
	if (argc != 1 && argc != 2 && argc != 3) { /* could generate assembly also */
		(void)fprintf(stderr, "usage: %s in.pas? out.bin?\n", argv[0]);
		return 1;
	}

	if (argc >= 2)
		c.in  = fopen_or_die(argv[1], "rb");
	if (argc >= 3)
		c.out = fopen_or_die(argv[2], "wb");

	if (compile(&c) < 0)
		r = 2;

	if (fclose(c.in) < 0)
		r = 3;
	if (fclose(c.out) < 0)
		r = 4;
	return r;
}

