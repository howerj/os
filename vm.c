/* Tiny 64-bit stack-based virtual machine with an MMU
 * Author: Richard James Howe
 * License: MIT
 * Repository: https//github.com/howerj/vm
 *
 * NOTES:
 * - More optional I/O could be added, such as networking, mouse, keyboard
 *   and sound, however it would need to be kept away from this (relatively)
 *   portable C code. Video memory could be added in a portable way.
 * - Some of the I/O is more realistic than others, the Virtual Machine is
 *   designed so that it should be possible to port to an FPGA unaltered,
 *   even if it would be difficult. */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MEMORY_START (0x0000080000000000ull)
#define MEMORY_END   (MEMORY_START + (sizeof (((vm_t) { .pc = 0 }).m)/ sizeof (uint64_t)))
#define IO_START     (0x0000040000000000ull)
#define IO_END       (MEMORY_START)
#define NELEMS(X)    (sizeof (X) / sizeof ((X)[0]))
#define BUILD_BUG_ON(condition)   ((void)sizeof(char[1 - 2*!!(condition)]))
#define PAGE         (8192ull)
#define PAGE_MASK    (PAGE - 1ull)
#define TLB_ADDR_MASK (0x0000FFFFFFFFFFFFull & ~PAGE_MASK)

typedef struct {
	uint64_t m[1024 * 1024];
	uint64_t pc, tos, sp, flags;
	
	uint64_t tick, timer, uart, tron, trap;
	uint64_t disk[1024 * 1024], dbuf[1024], dstat, dp;
	uint64_t traps[256];
	uint64_t tlb_va[64], tlb_pa[64];
	uint64_t rtc_last_s, rtc_s, rtc_frac_s;
	FILE *in, *out, *trace;
	int halt;
} vm_t;

enum { VIRT, PRIV, INTR,/* <- privileged flags */ N = 16, Z, C, V,  };
enum { TEMPTY, TIMPL, TDIV0, TINST, TADDR, TALIGN, TPRIV, TPROTECT, TUNMAPPED, TTIMER, TDISK, };
enum { READ, WRITE, EXECUTE };
enum { TLB_BIT_IN_USE = 48, TLB_BIT_PRIVILEGED, TLB_BIT_ACCESSED, TLB_BIT_DIRTY, TLB_BIT_READ, TLB_BIT_WRITE, TLB_BIT_EXECUTE, };

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
	return ch == DELETE ? BACKSPACE : ch;
}

static int wrap_putch(vm_t *v, const int ch) {
	assert(v);
	const int r = v->out ? fputc(ch, v->out) : putch(ch);
	bit_cnd(&v->uart, 8, r < 0 ? 0 : 1);
	return r;
}

static int trace(vm_t *v, const char *fmt, ...) {
	assert(v);
	assert(fmt);
	if (bit_get(v->tron, 0) == 0)
		return 0;
	if (!v->trace)
		return 0;
	va_list ap;
	va_start(ap, fmt);
	const int r1 = vfprintf(v->trace, fmt, ap);
	va_end(ap);
	const int r2 = fputc('\n', v->trace);
	if (r1 < 0 || r2 < 0) {
		v->halt = -2;
		return r1;
	}
	return r1 + 1;
}

static int push(vm_t *v, uint64_t val);

static int trap(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);

	if (trace(v, "+trap,%d,%"PRIx64",%"PRIx64",", v->trap, addr, val) < 0)
		return -1;

	if (v->trap > 2) {
		v->halt = -1;
		return -1;
	}
	v->trap++;
	if (addr == TADDR) {
		v->tos = val;
	} else { /* TODO: Only push PC? */
		if (push(v, v->flags))
			return 1;
		if (push(v, v->pc))
			return 1;
		if (push(v, v->tos))
			return 1;
		if (push(v, val))
			return 1;
	} 

	bit_set(&v->flags, PRIV);

	if (addr >= NELEMS(v->traps)) {
		v->halt = -1;
		return -1;
	}
	v->pc = v->traps[addr];
	return 1;
}

static int load_phy(vm_t *v, uint64_t addr, uint64_t *val) {
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
		case 2: *val = NELEMS(v->tlb_va); return 0;
		case 3: *val = 0x3; return 0; /* UART + DISK available */ 

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
		case 23: *val = 0; return 0;
		case 24: *val = v->rtc_s; return 0;
		case 25: *val = v->rtc_frac_s; return 0;
		case 26: *val = v->tron; return 0;
		}

		if (within(addr, 1024, 1024 + NELEMS(v->traps))) {
			*val = v->traps[addr - 1024];
			return 0;
		}
		
		BUILD_BUG_ON(2048 < 1024 + NELEMS(v->traps));

		if (within(addr, 2048, 2048 + NELEMS(v->dbuf))) {
			*val = v->dbuf[addr - 2048];
			return 0;
		}

		return trap(v, TADDR, addr);
	}
	return trap(v, TADDR, addr);
}

static int tlb_lookup(vm_t *v, uint64_t vaddr, uint64_t *paddr, int rwx) {
	assert(v);
	assert(paddr);

	BUILD_BUG_ON(sizeof (v->tlb_va) != sizeof (v->tlb_pa));
	const uint64_t lo = vaddr & TLB_ADDR_MASK;

	*paddr = 0;
	for (size_t i = 0; i < NELEMS(v->tlb_va); i++) {
		const uint64_t tva = v->tlb_va[i];
		const uint64_t pva = v->tlb_pa[i];
		if (bit_get(tva, TLB_BIT_IN_USE) == 0)
			continue;
		if (lo & (TLB_ADDR_MASK & tva))
			continue;
		if (bit_get(v->flags, PRIV))
			if (bit_get(tva, TLB_BIT_PRIVILEGED) == 0)
				return trap(v, TPROTECT, vaddr);
		int bit = 0;
		switch (rwx) {
		case READ:     bit = TLB_BIT_READ;    break;
		case WRITE:    bit = TLB_BIT_WRITE;   break;
		case EXECUTE:  bit = TLB_BIT_EXECUTE; break;
		}
		if (bit_get(tva, bit) == 0)
			return trap(v, TPROTECT, vaddr);
		bit_set(&v->tlb_va[i], TLB_BIT_ACCESSED);
		if (rwx == WRITE)
			bit_set(&v->tlb_va[i], TLB_BIT_DIRTY);
		*paddr = pva;
	}
	/* The MIPs way is to throw an exception and let the software
	 * deal with the problem, the fault handler cannot throw memory
	 * faults obviously. */
	return trap(v, TUNMAPPED, vaddr);
}

static int tlb_flush_single(vm_t *v, uint64_t vaddr, uint64_t *found) {
	assert(v);
	assert(found);
	*found = 0;
	if (bit_get(v->flags, PRIV) == 0)
		return trap(v, TPRIV, vaddr);
	BUILD_BUG_ON(sizeof (v->tlb_va) != sizeof (v->tlb_pa));
	vaddr &= TLB_ADDR_MASK;
	for (size_t i = 0; i < NELEMS(v->tlb_va); i++)
		if (vaddr == (v->tlb_va[i] & TLB_ADDR_MASK)) {
			bit_clr(&v->tlb_va[i], TLB_BIT_IN_USE);
			*found = 1;
			return 0;
		}
	return 0;
}

static int tlb_flush_all(vm_t *v) {
	assert(v);
	if (bit_get(v->flags, PRIV) == 0)
		return trap(v, TPRIV, 0);
	memset(v->tlb_va, 0, sizeof v->tlb_va);
	memset(v->tlb_pa, 0, sizeof v->tlb_va);
	return 0;
}

static int load(vm_t *v, uint64_t addr, uint64_t *val, int rwx) {
	assert(v);
	assert(val);
	if (bit_get(v->flags, VIRT))
		if (tlb_lookup(v, addr, &addr, rwx))
			return 1;
	return load_phy(v, addr, val);
}

static int loadb(vm_t *v, uint64_t addr, uint8_t *val) {
	assert(v);
	assert(val);
	uint64_t u64 = 0;
	if (load(v, addr & ~7ull, &u64, READ))
		return 1;
	*val = (u64 >> (addr % 8ull * CHAR_BIT));
	return 0;
}

static int store_phy(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);
	if (addr & 7ull)
		return trap(v, TALIGN, addr);

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
		case 22: 
			/* busy flag not used */
			v->dstat = val & 0xD;
			if (bit_get(val, 1)) {
				if ((v->dp / sizeof (uint64_t)) > (sizeof (v->disk) / sizeof(v->dbuf)))
					return trap(v, TDISK, v->dp);

				BUILD_BUG_ON((sizeof(v->disk) % sizeof(v->dbuf) != 0));

				if (bit_get(val, 2)) {
					memcpy(&v->disk[v->dp / sizeof (uint64_t)], &v->dbuf[0], sizeof v->dbuf);
				} else {
					memcpy(&v->dbuf[0], &v->disk[v->dp / sizeof (uint64_t)], sizeof v->dbuf);
				}
			}

			return 0;
		case 23: /* enable bit (0) not used */
			 if (bit_get(val, 1)) {
				const uint64_t cur_s = time(NULL);
				v->rtc_s += (cur_s - v->rtc_last_s);
				v->rtc_last_s = cur_s;
			 }
			 return 0;
		case 24: v->rtc_s = val; return 0;
		case 25: v->rtc_frac_s = val; return 0;
		case 26: v->tron = val; return 0;
		}

		if (within(addr, 1024, 1024 + NELEMS(v->traps))) {
			v->traps[addr - 1024] = val;
			return 0;
		}
		
		BUILD_BUG_ON(2048 < 1024 + NELEMS(v->traps));

		if (within(addr, 2048, 2048 + NELEMS(v->dbuf))) {
			v->dbuf[addr - 2048] = val;
			return 0;
		}


		return trap(v, TADDR, addr);
	}
	return trap(v, TADDR, addr);
}

static int store(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);
	if (bit_get(v->flags, VIRT))
		if (tlb_lookup(v, addr, &addr, WRITE))
			return 1;
	return store_phy(v, addr, val);
}

static int storeb(vm_t *v, uint64_t addr, uint8_t val) {
	assert(v);
	assert(val);
	uint64_t orig = 0;
	if (load(v, addr & (~7ull), &orig, READ))
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
	v->sp -= sizeof (uint64_t);
	return store(v, loc, val);
}

static int pop(vm_t *v, uint64_t *val) {
	assert(v);
	assert(val);
	*val = v->tos;
	v->sp += sizeof (uint64_t);
	return load(v, v->sp, val, READ);
}

static int cpu(vm_t *v) {
	assert(v);
	uint64_t instr = 0, next = v->pc + sizeof(uint64_t);
	if (load(v, v->pc, &instr, EXECUTE))
		return 1;
	if (trace(v, "+pc,%"PRIx64",%"PRIx64",%"PRIx64",", v->pc, instr, v->tos) < 0)
		return -1;

	const uint16_t op = instr >> (64 - 16);
	const uint64_t op1 = instr & 0x0000FFFFFFFFFFFFull;
	uint64_t a = v->tos, b = op1, c = 0;

	if ((op & 0x0800) && !bit_get(v->flags, V))
		goto next;
	if ((op & 0x0400) && !bit_get(v->flags, C))
		goto next;
	if ((op & 0x0200) && !bit_get(v->flags, Z))
		goto next;
	if ((op & 0x0100) && !bit_get(v->flags, N))
		goto next;

	if (op & 0x0080) /* pop instead of using operand */
		if (pop(v, &b))
			return 1;

	if ((op & 0x1000)) /* extend */
		if (b & 0x0000800000000000ull)
			b |= 0xFFFF000000000000ull;

	if (op & 0x4000) /* relative */
		b += v->pc;

	if (op & 0x8000) { /* jump */
		if (op & 0x2000) /* call */
			if (push(v, next))
				return 1;
		v->pc = b;
		return 0;
	}
	/* ALU operation */

	switch (op & 127) {
	case  0: c = a;      break;
	case  1: c = b;      break;
	case  2: c = ~a;     break;
	case  3: c = a & b;  break;
	case  4: c = a | b;  break;
	case  5: c = a ^ b;  break;
	case  6: a += bit_get(v->flags, C); /* fall-through */
	case  7: c = a + b; bit_cnd(&v->flags, C, c < a); bit_cnd(&v->flags, V, ((c ^ a) & (c ^ b)) >> 63); break;
	case  8: a -= bit_get(v->flags, C); /* fall-through */
	case  9: c = a - b; bit_cnd(&v->flags, C, c > a); bit_cnd(&v->flags, V, ((c ^ a) & (c ^ b)) >> 63);  break;
	case 10: c = a << b; break;
	case 11: c = a >> b; break;
	case 12: c = a * b;  break;
	case 13: if (!b) return trap(v, TDIV0, 0); c = a / b; break;
	case 14: c = v->pc; break;
	case 15: next = b; break;
	case 16: c = v->sp; break;
	case 17: v->sp = b; break;
	case 18: c = v->flags; break;
	case 19: 
		 if (bit_get(v->flags, PRIV)) { 
			 v->flags = b;
		 } else {
			if (b & 0xFFFFull)
				return trap(v, TPRIV, 0);
			v->flags |= (b & ~0xFFFFull);
		 }
		 break;
	case 20: return trap(v, b, a);
	case 21: v->trap = b & 0xFF; break;
	case 22: if (load(v, b, &c, READ)) return 1; break;
	case 23: if (store(v, b, a)) return 1; break;
	case 24: { uint8_t cb = 0; if (loadb(v, b, &cb)) return 1; c = cb; } break;
	case 25: if (storeb(v, b, a)) return 1; break;
	case 26: if (tlb_flush_single(v, b, &c)) return 1; break;
	case 27: if (tlb_flush_all(v)) return 1; break;
	case 28:
		if (bit_get(v->flags, PRIV) == 0) 
			return trap(v, TPRIV, op);
		const int va = bit_get(a, 15);
		bit_clr(&a, 15);
		if (a > NELEMS(v->tlb_va))
			return trap(v, TINST, op);
		if (va)
			v->tlb_va[a] = b;
		else
			v->tlb_pa[a] = b;
		break;

	/* the floating point instructions add, subtract, multiply and divide
	 * could be added, but they would need to be written in C to be
	 * portable */
	default:
		return trap(v, TINST, op);
	}
	if (op & 0x2000)
		if (push(v, c))
			return 1;
	if (c == 0)
		bit_set(&v->flags, Z);
	if ((c & (1ull << 63)))
		bit_set(&v->flags, N);

next:
	v->pc = next;
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

static int init(vm_t *v, FILE *in, FILE *out, FILE *trace) {
	assert(v);
	memset(v, 0, sizeof *v);
	v->flags      = 1ull << PRIV;
	v->pc         = MEMORY_START;
	v->sp         = MEMORY_END - sizeof(uint64_t);
	v->in         = in;
	v->out        = out;
	v->trace      = trace;
	v->rtc_last_s = time(NULL);
	v->rtc_s      = v->rtc_last_s;
	v->sp         = MEMORY_END - sizeof(uint64_t);
	return 0;
}

int main(int argc, char **argv) {
	static vm_t v;
	if (argc != 2) {
		(void)fprintf(stderr, "usage: %s disk\n", argv[0]);
		return 1;
	}
	if (init(&v, NULL, stdout, stdout) < 0)
		return 1;

	const size_t sz = sizeof(v.disk) / sizeof(v.disk[0]);
	const size_t mb = sizeof(v.disk[0]);

	FILE *loadme = fopen(argv[1], "rb");
	if (loadme) {
		fread(v.disk, mb, sz, loadme);
		memcpy(v.m, v.disk, 1024 * 8); /* BIOS */
		if (fclose(loadme) < 0)
			return 1;
	}
	const int r = run(&v, 0) < 0 ? 1 : 0;
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

