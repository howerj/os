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
#define LEXER_DEBUG   (1)

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
	PROGRAM, 
};

static char *rules[] = {
	"program", 
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

static int save(compile_t *c) {
	assert(c);
	const size_t elements = (c->start - MEMORY_START) / sizeof (uint64_t);
	assert(elements < NELEMS(c->m));
	for (size_t i = 0; i < elements; i++)
		if (fprintf(c->out, "%16"PRIx64"\n", c->m[i]) < 0)
			return warn(c, "failed to save");
	return 0;
}

static int program(compile_t *c, ast_t **r) { /* block ("." | EOI) */
	assert(c);
	assert(r);
	*r = NULL;
	return -1;
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

static int code(compile_t *c, ast_t *a, scope_t *s) {
	assert(c);
	assert(a);
	assert(s);
	return -1;
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

