/* Throw away assembler, it would have probably been easier to make a Forth
 * interpreter, and more useful, for example, see
 * <https://github.com/howerj/third> which is a de-obfuscated IOCCC winner. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_START  (0x0000080000000000ull)
#define IO_START      (0x0000040000000000ull)
#define MEMORY_SIZE   (1024 * 1024)
#define NELEMS(X)     (sizeof(X) / sizeof(X[0]))
#define LABELSZ       (64)
#define LABELMAX      (2048) /* lazy */
#define LINESZ        (1024)
#define implies(P, Q) (assert(!(P) || (Q)))

typedef struct {
	char name[LABELSZ];
	uint64_t pc, data;
	int relative, used;
} patch_t;

enum { LOCATION, DATA, CONST, };

typedef struct {
	char name[LABELSZ];
	uint64_t location;
	int used, type;
} label_t;

typedef struct {
	uint64_t pc;
	uint64_t *m;
	patch_t *p;
	label_t *l;
	size_t mlen, plen, llen;
	FILE *in, *out, *err;
	unsigned line;
} vm_t;

static int warn(vm_t *v, const char *fmt, ...) {
	if (!(v->err))
		return 0;
	va_list ap;
	const int r1 = fprintf(v->err, "%u: ", v->line);
	va_start(ap, fmt);
	const int r2 = vfprintf(v->err, fmt, ap);
	va_end(ap);
	const int r3 = fputc('\n', v->err);
	if (r1 < 0 || r2 < 0 || r3 < 0)
		return -1;
	//return r1 + r2 + 1;
	return -1;
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

static int casecmp(const char *a, const char *b) {
	assert(a);
	assert(b);
	for (size_t i = 0; ; i++) {
		const int ach = tolower(a[i]);
		const int bch = tolower(b[i]);
		const int diff = ach - bch;
		if (!ach || diff)
			return diff;
	}
	return 0;
}

static int patch_add(vm_t *v, char *name, uint64_t loc, int relative) {
	assert(v);
	assert(name);
	implies(v->plen > 0, v->plen);
	for (size_t i = 0; i < v->plen; i++) {
		patch_t *p = &v->p[i];
		if (p->used)
			continue;
		const size_t nl = strlen(name);
		if (nl > (sizeof (p->name) - 1))
			return warn(v, "name too long -- %s", name);
		memcpy(p->name, name, nl + 1);
		p->relative = relative;
		p->pc       = loc;
		p->used     = 1;
		return 0;
	}
	return -1;
}

static int label_add(vm_t *v, char *name, uint64_t loc, int used) {
	assert(v);
	assert(name);
	implies(v->llen > 0, v->llen);

	for (size_t i = 0; i < v->llen; i++) {
		label_t *l = &v->l[i];
		if (l->used)
			continue;
		const size_t nl = strlen(name);
		if (nl > (sizeof (l->name) - 1))
			return warn(v, "name too long -- %s", name);
		memcpy(l->name, name, nl + 1);
		l->used     = used;
		l->location = loc;
		l->used     = 1;
		return 0;
	}
	return -1;
}

static label_t *label_lookup(vm_t *v, const char *name) {
	assert(v);
	assert(name);
	for (size_t i = 0; i < v->llen; i++) {
		label_t *l = &v->l[i];
		if (!(l->used))
			return NULL;
		if (!casecmp(l->name, name))
			return l;
	}
	return NULL;
}

static int comment(char *line) {
	assert(line);
	for (size_t i = 0; line[i]; i++)
		if (line[i] == ';' || line[i] == '#')
			line[i] = '\0';
	int empty = 1;
	for (size_t i = 0; line[i]; i++)
		if (!isspace(line[i]))
			empty = 0;
	return empty;
}

static int convert(const char *line, uint64_t *val) {
	assert(line);
	assert(val);
	*val = 0;
	if (sscanf(line, "$%"SCNx64, val) == 1)
		return 1;
	if (sscanf(line, "%"SCNu64, val) == 1)
		return 1;
	return 0;
}

static int string_is(const char *line) {
	assert(line);
	const size_t sz = strlen(line);
	if (sz > 2 && line[0] == '"' && line[sz - 1] == '"')
		return 1;
	return 0;
}

static int emit(vm_t *v, uint64_t instruction) {
	assert(v);
	implies(v->mlen, v->m);
	if (v->pc & 7ull)
		return warn(v, "unaligned instruction");

	if (((v->pc - MEMORY_START) / sizeof (uint64_t)) > v->mlen)
		return warn(v, "not enough memory: %ld %ld", (long)v->pc, (long)v->mlen);

	v->m[(v->pc - MEMORY_START)/ sizeof (uint64_t)] = instruction;
	v->pc += sizeof (uint64_t);
	return 0;
}

static int string_emit(vm_t *v, const char *s) {
	assert(v);
	assert(s);
	const size_t sz = strlen(s);
	if (emit(v, sz) < 0)
		return -1;
	for (size_t i = 0; i < sz; i += 8) {
		uint64_t p = 0;
		for (size_t j = i, k = 0; k < 8 && j < sz; j++, k++) {
			const uint64_t ch = s[j];
			p |= (ch << (k * CHAR_BIT));
		}
		if (emit(v, p) < 0)
			return -1;
	}
	return 0;
}

static int directives(vm_t *v, int ops, char *op, char *op1, char *op2) {
	assert(v);
	assert(ops >= 1);
	assert(op);
	assert(op1);
	assert(op2);
	if (!casecmp(op, ".label")) {
		if (ops != 2)
			goto fail;
		if (label_add(v, op1, v->pc, LOCATION) < 0)
			return -1;
		return 1;
	}
	if (!casecmp(op, ".const")) {
		if (ops != 3)
			goto fail;
		uint64_t val = 0;
		if (!convert(op2, &val))
			return warn(v, "not a number -- %s", op2);
		if (label_add(v, op1, val, CONST) < 0)
			return -1;
		return 1;
	}
	if (!casecmp(op, ".db")) {
		if (ops != 2)
			goto fail;
		if (string_is(op1)) {
			const size_t sz = strlen(op1);
			op1[sz] = '\0';
			if (string_emit(v, op1 + 1) < 0)
				return -1;
			return 1;
		}
		uint64_t val = 0;
		if (!convert(op1, &val))
			return warn(v, "not a number -- %s", op1);
		if (emit(v, val) < 0)
			return -1;
		return 1;
	}
	return 0;
fail:
	return warn(v, "invalid operation -- %s %s %s", op, op1, op2);
}

static int label_resolve(vm_t *v, label_t *l, uint64_t pc, int relative, uint64_t *result) {
	assert(v);
	assert(l);
	assert(result);
	*result = 0;
	switch (l->type) {
	case LOCATION:
	case DATA: {
		uint64_t r = l->location;
		if (relative) {
			r -= pc;
			r &= ~0xFFFF000000000000ull;
		}
		*result = r;
		return 0;
	}
	case CONST:
		*result = l->location;
		return 0;
	}
	return warn(v, "invalid type -- %d", l->type);
}

static int instruction(vm_t *v, int ops, char *op, char *op1, char *op2) {
	assert(v);
	assert(ops >= 1);
	assert(op);
	assert(op1);
	assert(op2);

	static const char *instructions[] = {
		"a", "b", "invert", "and", "or", "xor", "addc", "add", 
		"subc", "sub", "lshift", "rshift", "mul", "div", "pcload",
		"pcstore", "spload", "spstore", "flagsload", "flagsstore", "trap",
		"traplset", "load", "store", "loadb", "storeb", "tlbflshs",
		"tlbflsha", "tlbinstall",
	};

	uint16_t push = 0, pop = 0, extend = 0, relative = 0, N = 0, Z = 0, C = 0, V = 0;
	char *flags = "";
	for (int i = 0; op[i]; i++) {
		if (op[i] == '.') {
			flags = &op[i + 1];
			op[i] = '\0';
			break;
		}
	}
	for (int i = 0, ch = 0; (ch = flags[i]); i++) {
		ch = tolower(ch);
		switch (ch) {
		case 'p': push     = 1; break;
		case 'u': pop      = 1; break;
		case 'r': relative = 1; break;
		case 'e': extend   = 1; break;
		case 'n': N        = 1; break;
		case 'z': Z        = 1; break;
		case 'c': C        = 1; break;
		case 'v': V        = 1; break;
		default:
			warn(v, "invalid flag -- %c", ch);
			goto fail;
		}
	}
	uint16_t opcode = 0;
	opcode |= pop      <<  7;
	opcode |= N        <<  8;
	opcode |= Z        <<  9;
	opcode |= C        << 10;
	opcode |= V        << 11;
	opcode |= extend   << 12;
	opcode |= push     << 13;
	opcode |= relative << 14;

	if (ops == 1 && pop == 0)
		goto fail;
	else if (ops != 2)
		goto fail;

	uint64_t operand = 0;

	if (!convert(op1, &operand)) {
		label_t *l = label_lookup(v, op1);
		if (!l) {
			if (patch_add(v, op1, v->pc, relative) < 0)
				return -1;
		} else {
			if (label_resolve(v, l, v->pc, relative, &operand) < 0)
				return -1;
		}
	}
	if (!casecmp(op, "j")) {
		opcode |= 1 << 15;
		if (emit(v, (((uint64_t)opcode) << 48) | operand) < 0)
			return -1;
		return 1;
	}
	for (size_t i = 0; i < NELEMS(instructions); i++) {
		if (!casecmp(op, instructions[i])) {
			opcode |= i;
			if (emit(v, (((uint64_t)opcode) << 48) | operand) < 0)
				return -1;
			return 1;
		}
	}

	return 0;
fail:
	return warn(v, "invalid operation -- %s %s %s", op, op1, op2);
}

static int patch(vm_t *v) {
	assert(v);
	for (size_t i = 0; i < v->plen; i++) {
		patch_t *p = &v->p[i];
		if (!(p->used))
			break;
		label_t *l = label_lookup(v, p->name);
		if (!l)
			return warn(v, "could not patch label %s", p->name);
		uint64_t operand = 0;
		if (label_resolve(v, l, p->pc, p->relative, &operand) < 0)
			return -1;
		const uint64_t addr = (p->pc - MEMORY_START) / sizeof(uint64_t);
		uint64_t instr = v->m[addr];
		instr |= operand & ~0xFFFF000000000000ull;
		v->m[addr] = instr;
	}
	return 0;
}

static int assemble(vm_t *v) {
	assert(v);
	assert(v->in);
	assert(v->out);

	for (char line[LINESZ]; fgets(line, sizeof line, v->in); line[0] = '\0') {
		v->line++;
		if (comment(line))
			continue;

		char op[32] = { 0, }, op1[LABELSZ] = { 0, }, op2[LABELSZ] = { 0, };
		int ops = sscanf(line, "%s %s %s", op, op1, op2);
		if (ops < 0)
			return warn(v, "invalid scan line -- %s", line);
		const int d = directives(v, ops, op, op1, op2);
		if (d < 0)
			return -1;
		if (d)
			continue;
		const int i = instruction(v, ops, op, op1, op2);
		if (i < 0)
			return -1;
		if (i)
			continue;
		return warn(v, "not an instruction or directive -- %s", line);
	}
	if (patch(v) < 0)
		return -1;
	const size_t used = (v->pc - MEMORY_START) / sizeof (uint64_t);
	for (size_t i = 0; i < used; i++)
		if (fprintf(v->out, "%016"PRIX64"\n", v->m[i]) < 0)
			return warn(v, "could not write to output");
	return 0;
}

int main(int argc, char **argv) {
	int r = 0;
	static label_t labels[LABELMAX];
	static patch_t patches[LABELMAX];
	static uint64_t memory[MEMORY_SIZE];
	static vm_t v = { .pc = MEMORY_START };
	v.l    = labels;
	v.p    = patches;
	v.m    = memory;
	v.mlen = NELEMS(memory);
	v.llen = NELEMS(labels);
	v.plen = NELEMS(patches);
	v.in   = stdin;
	v.out  = stdout;
	v.err  = stderr;

	if (argc != 1 && argc != 2 && argc != 3) {
		(void)fprintf(stderr, "usage: %s in.asm? out.hex?\n", argv[0]);
		return 1;
	}

	if (argc == 2 || argc == 3)
		v.in = fopen_or_die(argv[1], "rb");
	if (argc == 3)
		v.out = fopen_or_die(argv[2], "wb");
	if (assemble(&v) < 0)
		r = 2;
	if (fclose(v.in))
		r = 3;
	if (fclose(v.out))
		r = 4;
	return r;
}
