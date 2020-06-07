#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TODO: MMU, Interrupts, timer, traps, finished instruction set, 
 * test program, more (optional) I/O (networking, RTC, mouse, keyboard, sound),
 * tracing and debugging, load and store byte, more realistic I/O, CPU state */

struct vm;

typedef struct vm {
	uint64_t m[1024 * 1024];
	uint64_t pc, tos, sp, flags;
	
	uint64_t tick, timer, uart;
	uint64_t disk[1024 * 1024], dbuf[1024], dstat, dp;
	uint64_t traps[256];
	FILE *in, *out;
	int halt, trap;
} vm_t;

#define MEMORY_START (0x0000080000000000ull)
#define MEMORY_END   (MEMORY_START + (sizeof (((vm_t) { .pc = 0 }).m)/ sizeof (uint64_t)))
#define IO_START     (0x0000040000000000ull)
#define IO_END       (MEMORY_START)
#define NELEMS(X)    (sizeof (X) / sizeof ((X)[0]))
#define BUILD_BUG_ON(condition)   ((void)sizeof(char[1 - 2*!!(condition)]))

enum { REAL, PRIV, INTR,/* <- privileged flags */ N = 16, Z, C, O,  };
enum { TEMPTY, TDIV0, TINST, TADDR, TALIGN, TIMPL, TPRIV, TTIMER, };

static inline int within(uint64_t addr, uint64_t lo, uint64_t hi) { return addr >= lo && addr < hi; }

static inline int bit_get(uint64_t v, int bit) { return !!(v & (1ull << bit)); }
static inline int bit_set(uint64_t *v, int bit) { assert(v); *v = *v |  (1ull << bit); return 1; }
static inline int bit_xor(uint64_t *v, int bit) { assert(v); *v = *v ^  (1ull << bit); return bit_get(*v, bit); }
static inline int bit_clr(uint64_t *v, int bit) { assert(v); *v = *v & ~(1ull << bit); return 0; }
static inline int bit_cnd(uint64_t *v, int bit, int set) { assert(v); return (set ? bit_set : bit_clr)(v, bit); }

#ifdef __unix__
#include <unistd.h>
#include <termios.h>
static int getch(void) {
	struct termios oldattr, newattr;
	tcgetattr(STDIN_FILENO, &oldattr);
	newattr = oldattr;
	newattr.c_iflag &= ~(ICRNL);
	newattr.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newattr);
	const int ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldattr);
	return ch;
}

static int putch(int c) {
	int res = putchar(c);
	fflush(stdout);
	return res;
}
#else
#ifdef _WIN32
extern int getch(void);
extern int putch(int c);
#else
static int getch(void) { return getchar(); }
static int putch(const int c) { return putchar(c); }
#endif
#endif /** __unix__ **/

enum { BACKSPACE = 8, ESCAPE = 27, DELETE = 127, };

static int wrap_getch(vm_t *v) {
	assert(v);
	const int ch = v->in ? fgetc(v->in) : getch();
	bit_cnd(&v->uart, 9, ch < 0 ? 0 : 1);
	//if (ch == EOF || ch == ESCAPE)
	//	v->halt = 1;
	return ch == DELETE ? BACKSPACE : ch;
}

static int wrap_putch(vm_t *v, const int ch) {
	assert(v);
	const int r = v->out ? fputc(ch, v->out) : putch(ch);
	bit_cnd(&v->uart, 8, r < 0 ? 0 : 1);
	return r;
}

static int push(vm_t *v, uint64_t val);

static int trap(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);
	if (v->trap > 2) {
		v->halt = -1;
		return -1;
	}
	v->trap++;
	if (addr != TADDR) {
		push(v, v->flags);
		push(v, v->pc);
		push(v, v->tos);
		push(v, val);
	} else {
		v->tos = val;
	}

	bit_set(&v->flags, PRIV);
	v->pc = addr;
	v->trap--;
	return 1;
}

static int load_phy(vm_t *v, uint64_t addr, uint64_t *val, int exe) {
	assert(v);
	assert(val);
	*val = 0;
	if (addr & 7ull)
		return trap(v, TALIGN, addr);

	if (within(addr, MEMORY_START, MEMORY_END)) {
		addr -= MEMORY_START;
		addr /= sizeof(uint64_t);
		*val = v->m[addr];
		return 0;
	}

	if (within(addr, IO_START, IO_END)) {
		addr -= IO_START;
		addr /= sizeof(uint64_t);
		switch (addr) {
		case 0: *val = 0x1; return 0; /* version */ 
		case 1: *val = sizeof (v->m); return 0;
		case 2: *val = 0x3; return 0; /* UART + DISK available */ 

		case 16: *val = v->halt; return 0;
		case 17: *val = v->tick; return 0;
		case 18: *val = v->timer; return 0;
		case 19: *val = v->uart; return 0;
		case 20: *val = (int64_t)wrap_getch(v); return 0;
		case 21: *val = v->dp; return 0;
		case 22: 
			 bit_clr(&v->dstat, 0); /* never busy */
			 bit_clr(&v->dstat, 1); /* do operation always reads 0 */
			 *val = v->dstat & 0xF;
			 return 0;
		}

		if (within(addr, 1024, 1024 + NELEMS(v->traps))) {
			*val = v->traps[addr];
			return 0;
		}
		
		BUILD_BUG_ON(2048 < 1024 + NELEMS(v->traps));

		if (within(addr, 2048, 2048 + NELEMS(v->dbuf))) {
			*val = v->dbuf[addr];
			return 0;
		}

		return trap(v, TADDR, addr * 8);
	}
	return trap(v, TADDR, addr * 8);
}

static int load(vm_t *v, uint64_t addr, uint64_t *val, int exe) {
	assert(v);
	assert(val);
	if (bit_get(v->flags, REAL))
		return load_phy(v, addr, val, exe);
	return trap(v, TIMPL, addr);
}

static int loadb(vm_t *v, uint64_t addr, uint8_t *val) {
	assert(v);
	assert(val);
	uint64_t u64 = 0;
	if (load(v, addr & ~7ull, &u64, 0))
		return 1;
	*val = (u64 >> (addr % 8ull * CHAR_BIT));
	return 0;
}

static int store_phy(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);
	if (addr & 7ull)
		return trap(v, TALIGN, addr);
	addr /= sizeof(uint64_t);

	if (within(addr, MEMORY_START, MEMORY_END)) {
		addr -= MEMORY_START;
		addr /= sizeof(uint64_t);
		v->m[addr] = val;
		return 0;
	}
	if (within(addr, IO_START, IO_END)) {
		addr -= IO_START;
		addr /= sizeof(uint64_t);

		switch (addr) {
		case 16: v->halt = val; return 0;
		case 17: v->tick = val; return 0;
		case 18: v->timer = val; return 0;
		case 19: v->uart = 0; return 0;
		case 20: wrap_putch(v, val); return 0;
		case 21: v->dp = val; return 0;
		}

		if (within(addr, 1024, 1024 + NELEMS(v->traps))) {
			v->traps[addr] = val;
			return 0;
		}
		
		BUILD_BUG_ON(2048 < 1024 + NELEMS(v->traps));

		if (within(addr, 2048, 2048 + NELEMS(v->dbuf))) {
			v->dbuf[addr] = val;
			return 0;
		}


		return trap(v, TADDR, addr * 8);
	}
	return trap(v, TADDR, addr * 8);
}

static int store(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);
	if (bit_get(v->flags, REAL))
		return store_phy(v, addr, val);
	return trap(v, TIMPL, addr);
}

static int storeb(vm_t *v, uint64_t addr, uint8_t val) {
	assert(v);
	assert(val);
	uint64_t orig = 0;
	if (load(v, addr & (~7ull), &orig, 0))
		return 1;
	const unsigned shift = (addr & 7ull) * CHAR_BIT;
	orig &= ~(0xFFull << shift);
	orig |=  ((uint64_t)val) << shift;
	return store(v, addr & ~7ull, orig);
}

static int push(vm_t *v, uint64_t val) {
	assert(v);
	v->tos = val;
	const uint64_t loc = v->sp;
	v->sp += sizeof (uint64_t);
	return store(v, loc, val);
}

static int pop(vm_t *v, uint64_t *val) {
	assert(v);
	assert(val);
	*val = v->tos;
	v->sp -= sizeof (uint64_t);
	return load(v, v->sp, val, 0);
}

static int cpu(vm_t *v) {
	assert(v);
	const uint64_t next = v->pc + sizeof(uint64_t);
	uint64_t instr = 0;
	if (load(v, v->pc, &instr, 1) < 0)
		return 1;
	const uint16_t op = instr >> (64 - 16);
	const uint64_t op1 = instr & 0x0000FFFFFFFFFFFFull;
	v->pc = next;

	if ((op & 0x0800) && !bit_get(v->flags, O))
		return 0;
	if ((op & 0x0400) && !bit_get(v->flags, C))
		return 0;
	if ((op & 0x0200) && !bit_get(v->flags, Z))
		return 0;
	if ((op & 0x0100) && !bit_get(v->flags, N))
		return 0;

	uint64_t extend = op1;
	if ((op & 0x1000)) {
		if (extend & 0x0000800000000000ull)
			extend |= 0xFFFF800000000000ull;
	}

	if (op & 0x8000) { /* jump */
		uint64_t dst = extend;
		if (op & 0x2000) /* call */
			if (push(v, next))
				return 1;
		if (op & 0x4000) /* relative */
			dst = v->pc + extend;
		v->pc = dst;
		return 0;
	}
	/* ALU operation */

	uint64_t a = v->tos, b = 0, c = 0;

	if (op & 0x4000) {
		if (pop(v, &b))
			return 1;
		if ((op & 0x1000)) {
			if (b & 0x0000800000000000ull)
				b |= 0xFFFF800000000000ull;
		}
	} 
	b = extend;

	switch (op & 255) {
	case  0: break;
	case  1: c = ~a;     break;
	case  2: c = a & b;  break;
	case  3: c = a | b;  break;
	case  4: c = a ^ b;  break;
	case  5: c = a + b;  /*TODO: overflow */break;
	case  6: c = a - b;  /*TODO: underflow */break;
	case  7: c = a << b; break;
	case  8: c = a >> b; break;
	case  9: c = a * b;  break;
	case 10: if (!b) return trap(v, TDIV0, 0); c = a / b; break;
	case 11: c = v->pc; break;
	case 12: v->pc = b; break;
	case 13: c = v->sp; break;
	case 14: v->sp = b; break;
	case 15: c = v->flags; break;
	case 16: 
		 if (bit_get(v->flags, PRIV)) { 
			 v->flags = b;
		 } else {
			if (b & 0xFFFFull)
				return trap(v, TPRIV, 0);
			v->flags |= (b & ~0xFFFFull);
		 }
		 break;
	case 17: return trap(v, b, a);
	case 18: if (load(v, b, &c, 0)) return 1; break;
	case 19: if (store(v, b, c)) return 1; break;
	case 20: { uint8_t cb = 0; if (loadb(v, b, &cb)) return 1; c = cb; } break;
	case 21: if (storeb(v, b, a)) return 1; break;
	default:
		return trap(v, TINST, 0);
	}
	if (op & 0x2000)
		if (push(v, c))
			return 1;
	if (c == 0)
		bit_set(&v->flags, Z);
	if ((c & (1ull << 63)))
		bit_set(&v->flags, N);

	return 0;
}

static int interrupt(vm_t *v) {
	assert(v);

	if (v->timer) {
		if (v->tick >= v->timer) {
			v->tick = 0;
			if (bit_get(v->flags, INTR) == 0)
				return trap(v, TTIMER, v->timer);
		}
	}
	v->tick++;

	return 0;
}

static int run(vm_t *v, uint64_t step) {
	assert(v);
	int forever = step == 0;
	for (uint64_t i = 0; (i < step) || (forever && !v->halt); i++) {
		if (interrupt(v) < 0)
			return -1;
		if (cpu(v) < 0)
			return -1;
	}
	return v->halt;
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
	static vm_t v = { .halt = 0, .flags = (1ull << REAL) | (1ull << PRIV), };
	if (argc != 2) {
		(void)fprintf(stderr, "usage: %s disk\n", argv[0]);
		return 1;
	}
	v.in = NULL;
	v.out = stdout;
	const size_t sz = sizeof(v.disk) / sizeof(v.disk[0]);
	const size_t mb = sizeof(v.disk[0]);

	FILE *loadme = fopen(argv[1], "rb");
	if (loadme) {
		fread(v.disk, mb, sz, loadme);
		memcpy(v.m, v.disk, 1024 * 8); /* BIOS */
		if (fclose(loadme) < 0)
			return 1;
	}
	const int r = run(&v, 100) < 0 ? 1 : 0;
	if (r < 0)
		return 1;
	if (r == 1)
		return 0;
	FILE *saveme = fopen_or_die(argv[1], "wb");
	errno = 0;
	if (sz != fwrite(v.disk, mb, sz, saveme)) {
		(void)fclose(saveme);
		(void)fprintf(stderr, "Unable to write %ld words to file %s: %s\n", (long)sz, argv[1], strerror(errno));
		return 1;
	}
	if (fclose(saveme) < 0)
		return 1;
	return r;
}

