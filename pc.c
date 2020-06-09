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
 * records, arrays and strings. */

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
#define MEMORY_SIZE   (1024 * 1024)
#define NELEMS(X)     (sizeof(X) / sizeof(X[0]))
#define implies(P, Q) (assert(!(P) || (Q)))

struct ast;
struct record;
typedef struct ast ast_t;
typedef struct record record_t;

struct ast {
	int type;
	ast_t **as;
	size_t children;
	union {
		uint64_t d;
		char *s;
	} p;
};

typedef struct {
	uint64_t pc;
	uint64_t m[MEMORY_SIZE];
	unsigned line;
	ast_t *as;
	FILE *in, *out, *err;
	/* lexer */
	char buf[512];
	int type;
	char *str;
	uint64_t d;
} compile_t;

static int warn(compile_t *c, const char *fmt, ...) {
	assert(c);
	assert(fmt);
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

enum {
	INVALID, 
	INT, STR, IDENT, LPAR, RPAR, LBRC, RBRC, ASSIGN, SEP, DOT,
	PLUS, MINUS, LSHIFT, RSHIFT, MUL, DIV, EQ, NEQ, GT, GTE, LT, LTE, AND, OR, XOR, INVERT, /* rotate? */
	EOI,
	IF /* <- first keyword */, ELSE, DO, WHILE, PROCEDURE, FOR, VAR, CONST, BREAK, CONTINUE, ASSERT, IMPLIES,
};

static const char *keywords[] = {
	"if", "else", "do", "while", "procedure", "for", "var", "const", "break", "continue", "assert", "implies",
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
	free(c->str);
	c->str = NULL;
	int ch = 0;
again:
	ch = fgetc(c->in);
	switch (ch) {
	case '\n': c->line++; /* fall-through */
	case '\r': case ' ': case '\t': goto again;
	case ':': ch = fgetc(c->in);
		  if (ch != '=')
			  return warn(c, "expected '='");
		  c->type = ASSIGN; break;
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
	case '}': c->type = RPAR;   break;
	case '{': c->type = LPAR;   break;
	case ')': c->type = RBRC; break;
	case '(': ch = fgetc(c->in);
		if (ch == '*') { /* comment */
			for (;;) {
				ch = fgetc(c->in);
				if (ch < 0)
					return warn(c, "unexpected EOF");
				if (ch == '*' && fgetc(c->in) == ')')
					break;
			}
			goto again;
		}
		c->type = LBRC;
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
		for (i = 0; i < max; i++) {
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
		c->buf[max] = '\0';
		if (i >= max)
			return warn(c, "identifier %s... too long (%d bytes)", c->buf, i);
		c->type = STR;
		c->str = duplicate(c->buf);
		break;
	}
	default:
		if (!isalnum(ch))
			return warn(c, "invalid character -- %c", ch);

		if (ch >= '0' && ch <= '9') {
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
		c->buf[max] = '\0';
		if (i >= max)
			return warn(c, "identifier %s... too long (%d bytes)", c->buf, i);
		for (size_t i = 0; i < NELEMS(keywords); i++) {
			if (!strcmp(c->buf, keywords[i])) {
				c->type = i + IF;
				goto unget;
			}
		}
		c->type = IDENT;
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
		free(c->str);
		c->str = NULL;
		if (sym != EOI && sym != DOT)
			if (lexer(c) < 0)
				return -1;
		return 1;
	}
	return 0;
}

static int expect(compile_t *c, int sym) {
	assert(c);
	const int r = accept(c, sym);
	if (r)
		return r;
	return warn(c, "syntax error, expected %d, got %d", sym, c->type);
}

static inline int use(compile_t *c, ast_t *a) { /* move ownership of string from lexer to parse tree */
	assert(c);
	assert(a);
	a->p.s = c->str;
	c->str = NULL;
	return 0;
}

static ast_t *ast_new(compile_t *c, int type, size_t count) {
	assert(c);
	return NULL;
}

static ast_t *parse(compile_t *c) {
	assert(c);
	return NULL;
}

static void ast_free(ast_t **as, size_t length) {
	implies(length > 0, as);
	for (size_t i = 0; i < length; i++) {
		ast_t *a = as[i];
		assert(a);
		ast_free(a->as, a->children);
		free(a->as);
		if (a->type == IDENT || a->type == STR)
			free(a->p.s);
		free(a);
	}
	return ;
}

static int generate(compile_t *c) {
	assert(c);
	return -1;
}

static int save(compile_t *c) {
	assert(c);
	const size_t elements = (c->pc - MEMORY_SIZE) / sizeof (uint64_t);
	assert(elements < NELEMS(c->m));
	for (size_t i = 0; i < elements; i++)
		if (fprintf(c->out, "%16"PRIx64"\n", c->m[i]) < 0)
			return warn(c, "");
	return 0;
}

static int compile(compile_t *c) {
	assert(c);
	assert(c->in);
	assert(c->out);

	/* TODO: Remove this test program for the lexer */
	int r = lexer(c);
	do {
		if (r < 0)
			return -1;
		(void)fprintf(c->out, "lexed: %d ", c->type);
		if (c->type == INT)
			(void)fprintf(c->out, "%"PRIx64"/%"PRId64" ", c->d, c->d);
		if (c->type == IDENT || c->type == STR)
			(void)fprintf(c->out, "%s", c->str);
		(void)fputc('\n', c->out);
		r = lexer(c);
	} while (c->type != EOI);

	if (!(c->as = parse(c)))
		return -1;
	generate(c);
	ast_free(&c->as, 1);
	save(c);
	return 0;
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
	static compile_t c = { .pc = MEMORY_START,  };
	c.in  = stdin;
	c.out = stdout;
	c.err = stderr;
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

