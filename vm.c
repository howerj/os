/* Richard James Howe, howe.r.j.89@gmail.com, Virtual Machine, Public Domain */
/* TODO: CPU/MMU/Trap/Interrupts/Timer/RTC/Hard-drive
 * TODO: UART/Networking
 * TODO: Screen/Keyboard/Mouse/Sound
 * TODO: Build options for Pure C only version
 * TODO: Floating point */
#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// TODO: Move OS stuff to "os.c" file.
/*#include <pcap.h>
#include <SDL.h>*/

#ifdef __unix__
#include <unistd.h>
#include <termios.h>
static struct termios oldattr, newattr;

static void restore(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &oldattr);
}

static int setup(void) {
	tcgetattr(STDIN_FILENO, &oldattr);
	newattr = oldattr;
	newattr.c_iflag &= ~(ICRNL);
	newattr.c_lflag &= ~(ICANON | ECHO);
	newattr.c_cc[VMIN]  = 0;
	newattr.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &newattr);
	atexit(restore);
	return 0;
}

static int getch(void) {
	static int init = 0;
	if (!init) {
		setup();
		init = 1;
	}
	unsigned char r = 0;
	if (read(STDIN_FILENO, &r, 1) != 1)
		return -1;
	return r;
}

static int putch(int c) {
	int res = putchar(c);
	fflush(stdout);
	return res;
}

static void sleep_ms(unsigned ms) {
	usleep((unsigned long)ms * 1000);
}
#else
#ifdef _WIN32

extern int getch(void);
extern int putch(int c);
static void sleep_ms(unsigned ms) {
	usleep((unsigned long)ms * 1000);
}
#else
static int getch(void) {
	return getchar();
}

static int putch(const int c) {
	return putchar(c);
}

static void sleep_ms(unsigned ms) {
	(void)ms;
}
#endif
#endif /** __unix__ **/

static int wrap_getch(void) {
	const int ch = getch();
	if (ch == EOF) {
		sleep_ms(1);
	}
	if (ch == 27)
		exit(0);
	return ch == 127 ? 8 : ch;
}

static int wrap_putch(const int ch) {
	return putch(ch);
}

static inline int within(uint64_t addr, uint64_t lo, uint64_t hi) { return addr >= lo && addr < hi; }

static inline int bit_get(uint64_t v, int bit) { return !!(v & (1ull << bit)); }
static inline int bit_set(uint64_t *v, int bit) { assert(v); *v = *v |  (1ull << bit); return 1; }
static inline int bit_xor(uint64_t *v, int bit) { assert(v); *v = *v ^  (1ull << bit); return bit_get(*v, bit); }
static inline int bit_clr(uint64_t *v, int bit) { assert(v); *v = *v & ~(1ull << bit); return 0; }
static inline int bit_cnd(uint64_t *v, int bit, int set) { assert(v); return (set ? bit_set : bit_clr)(v, bit); }

#define MEMORY_START (0x0000080000000000ull)
#define MEMORY_END   (MEMORY_START + (sizeof (((vm_t) { .pc = 0 }).m)/ sizeof (uint64_t)))
#define IO_START     (0x0000040000000000ull)
#define IO_END       (MEMORY_START)
#define SIZE         (1024ul * 1024ul * 1ul)
#define TLB_ENTRIES  (64ul)
#define TRAPS        (32ul)
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#define PAGE_SIZE    (8192ull)
#define PAGE_MASK    (PAGE_SIZE - 1ull)
#define TLB_ADDR_MASK (0x0000FFFFFFFFFFFFull & ~PAGE_MASK)
#define NELEMS(X)    (sizeof (X) / sizeof ((X)[0]))

typedef struct {
	uint64_t m[SIZE / sizeof (uint64_t)];
	uint64_t pc, flags, tos, sp, level, timer, tick, signal, tron;
	uint64_t traps[TRAPS];
	uint64_t tlb_va[TLB_ENTRIES], tlb_pa[TLB_ENTRIES];
	uint64_t disk[SIZE / sizeof (uint64_t)], dbuf[PAGE_SIZE], dstat, dp;
	uint64_t uart_control, uart_rx, uart_tx;
	uint64_t rtc_control, rtc_s, rtc_frac_s;
	uint64_t loaded;
	int halt;
	FILE *trace;
} vm_t;
enum { READ, WRITE, EXECUTE };
enum { V = 52, C, Z, N, /* saved flags -> */ SVIRT = 56, SPRIV, SINTR, /* privileged flags -> */INTR = 60, PRIV, VIRT };
enum { T_GENERAL, T_ASSERT, T_IMPL, T_DIV0, T_INST, T_ADDR, T_ALIGN, T_PRIV, T_PROTECT, T_UNMAPPED, T_TIMER, };
enum { TLB_BIT_IN_USE = 48, TLB_BIT_PRIVILEGED, TLB_BIT_ACCESSED, TLB_BIT_DIRTY, TLB_BIT_READ, TLB_BIT_WRITE, TLB_BIT_EXECUTE, };

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
		return -1;
	}
	return r1 + 1;
}

static int trap(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);

	if (trace(v, "+trap,%"PRIx64",%"PRIx64",%"PRIx64",", v->level, addr, val) < 0)
		return -1;

	if (v->level > 2) {
		v->halt = -1;
		return -1;
	}

	v->level++;
	v->signal = val;
	v->flags &= 0xF0F0ull << 48; /* clear saved program counter and saved flags */
	v->flags |= v->flags >> 4;   /* save flags */
	v->flags |= v->pc;           /* save program counter */
	bit_set(&v->flags, PRIV);    /* escalate privilege level */

	if (addr >= NELEMS(v->traps))
		addr = T_ADDR;
	v->pc = v->traps[addr];
	return 1;
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
				return trap(v, T_PROTECT, vaddr);
		int bit = 0;
		switch (rwx) {
		case READ:     bit = TLB_BIT_READ;    break;
		case WRITE:    bit = TLB_BIT_WRITE;   break;
		case EXECUTE:  bit = TLB_BIT_EXECUTE; break;
		}
		if (bit_get(tva, bit) == 0)
			return trap(v, T_PROTECT, vaddr);
		bit_set(&v->tlb_va[i], TLB_BIT_ACCESSED);
		if (rwx == WRITE)
			bit_set(&v->tlb_va[i], TLB_BIT_DIRTY);
		*paddr = pva;
	}
	/* The MIPs way is to throw an exception and let the software
	 * deal with the problem, the fault handler cannot throw memory
	 * faults obviously. */
	return trap(v, T_UNMAPPED, vaddr);
}

static int tlb_flush_single(vm_t *v, uint64_t vaddr, uint64_t *found) {
	assert(v);
	assert(found);
	*found = 0;
	if (bit_get(v->flags, PRIV) == 0)
		return trap(v, T_PRIV, vaddr);
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
		return trap(v, T_PRIV, 0);
	memset(v->tlb_va, 0, sizeof v->tlb_va);
	memset(v->tlb_pa, 0, sizeof v->tlb_va);
	return 0;
}

#define IO(X, Y) ((((uint64_t)(X)) * (PAGE_SIZE / sizeof (uint64_t))) + (uint64_t)(Y))

static int load_phy(vm_t *v, uint64_t addr, uint64_t *val) {
	assert(v);
	assert(val);
	*val = 0;
	if (addr & 7ull)
		return trap(v, T_ALIGN, v->pc);

	if (within(addr, MEMORY_START, MEMORY_END)) {
		addr -= MEMORY_START;
		addr /= sizeof (uint64_t);
		*val = v->m[addr];
		return 0;
	}

	if (within(addr, IO_START, IO_END)) {
		addr -= IO_START;
		addr /= sizeof (uint64_t);
		switch (addr) {
		/* PAGE 0 = Info */
		case IO(0, 0): *val = 1; /* version */ return 0;
		case IO(0, 1): *val = sizeof (uint64_t); return 0;
		case IO(0, 2): *val = NELEMS(v->tlb_va); return 0;
		case IO(0, 3): *val = PAGE_SIZE; return 0;
		case IO(0, 4): *val = TRAPS; return 0;
		case IO(0, 5): *val = 0x3; return 0; /* available I/O:  UART + DISK available */ 
		/* PAGE 1 = Basic system registers */
		case IO(1, 0): *val = v->halt; return 0;
		case IO(1, 1): *val = v->tron; return 0;
		case IO(1, 2): *val = v->tick; return 0;
		case IO(1, 3): *val = v->timer; return 0;
		case IO(1, 4): *val = v->rtc_control; return 0;
		case IO(1, 5): *val = v->rtc_s; return 0;
		case IO(1, 6): *val = v->rtc_frac_s; return 0;
		/* PAGE 2 = UART */
		case IO(2, 0): *val = 0x4; /* bit 3 = RX queue not empty, bit 5 = TX queue not empty */ return 0;
		case IO(2, 1): *val = v->uart_rx; return 0;
		case IO(2, 2): *val = v->uart_rx; return 0;
		/* PAGE 3 = Traps */
		/* PAGE 4 = Disk Control */
		case IO(4, 0): *val = v->dstat; return 0;
		case IO(4, 1): 
			bit_clr(&v->dstat, 0); /* never busy */
			bit_clr(&v->dstat, 1); /* do operation always reads 0 */
			*val = v->dstat & 0x1Full;
			return 0;
		/* PAGE 5 = Disk Buffer */
		}

		if (within(addr, IO(3, 0), IO(3, 0) + (sizeof (v->traps)/sizeof (uint64_t)))) {
			*val = v->traps[addr - IO(3, 0)];
			return 0;
		}

		if (within(addr, IO(5, 0), IO(5, 0) + (sizeof (v->dbuf)/sizeof (uint64_t)))) {
			*val = v->dbuf[addr - IO(5, 0)];
			return 0;
		}
	}

	return trap(v, T_ADDR, v->pc);
}

static int store_phy(vm_t *v, uint64_t addr, uint64_t val) {
	assert(v);
	if (addr & 7ull)
		return trap(v, T_ALIGN, v->pc);

	if (within(addr, MEMORY_START, MEMORY_END)) {
		addr -= MEMORY_START;
		addr /= sizeof (uint64_t);
		v->m[addr] = val;
		return 0;
	}

	if (within(addr, IO_START, IO_END)) {
		addr -= IO_START;
		addr /= sizeof (uint64_t);

		switch (addr) {
		/* PAGE 0 = Info */
		/* PAGE 1 = Basic system registers */
		case IO(1, 0): v->halt = val; return 0;
		case IO(1, 1): v->tron = val; return 0;
		case IO(1, 2): v->tick = val; return 0;
		case IO(1, 3): v->timer = val; return 0;
		case IO(1, 4): if (val & 1) {
				v->rtc_s = time(NULL);
				v->rtc_frac_s = 0;
			       }; return 0;
		case IO(1, 5): v->rtc_s = val; return 0;
		case IO(1, 6): v->rtc_frac_s = val; return 0;
		/* PAGE 2 = UART */
		case IO(2, 0):
			if (val & 1ull)
				v->uart_rx = wrap_getch();
			if (val & 2ull)
				v->uart_tx = wrap_putch(v->uart_tx);
			return 0;
		case IO(2, 1): v->uart_rx = val; return 0;
		case IO(2, 2): v->uart_rx = val; return 0;
		/* PAGE 3 = Traps */
		/* PAGE 4 = Disk Control */
		case IO(4, 0): 
			BUILD_BUG_ON((sizeof(v->disk) % sizeof(v->dbuf) != 0));
			v->dstat = val & 0x1Dull;
			if (v->dstat & 0x10ull)
				return 0;
       			if (bit_get(val, 1)) {
				if ((v->dp / sizeof (uint64_t)) > (sizeof (v->disk) / sizeof(v->dbuf))) {
					v->dstat |= 0x10ull;
					return 0;
				}
				BUILD_BUG_ON((sizeof(v->disk) % sizeof(v->dbuf) != 0));

				if (bit_get(val, 2)) {
					memcpy(&v->disk[v->dp / sizeof (uint64_t)], &v->dbuf[0], sizeof v->dbuf);
				} else {
					memcpy(&v->dbuf[0], &v->disk[v->dp / sizeof (uint64_t)], sizeof v->dbuf);
				}
			}
			return 0;
		case IO(4, 1): v->dp = val; return 0;
		/* PAGE 5 = Disk Buffer */
		}

		if (within(addr, IO(3, 0), IO(3, 0) + (sizeof (v->traps)/sizeof (uint64_t)))) {
			v->traps[addr - IO(3, 0)] = val;
			return 0;
		}

		if (within(addr, IO(5, 0), IO(5, 0) + (sizeof (v->dbuf)/sizeof (uint64_t)))) {
			v->dbuf[addr - IO(5, 0)] = val;
			return 0;
		}
	}
	return trap(v, T_ADDR, v->pc);
}

static int loadw(vm_t *v, uint64_t addr, uint64_t *val, int rwx) {
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
	if (loadw(v, addr & ~7ull, &u64, READ))
		return 1;
	*val = (u64 >> (addr % 8ull * CHAR_BIT));
	return 0;
}

static int storew(vm_t *v, uint64_t addr, uint64_t val) {
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
	if (loadw(v, addr & (~7ull), &orig, READ))
		return 1;
	const unsigned shift = (addr & 7ull) * CHAR_BIT;
	orig &= ~(0xFFull << shift);
	orig |=  ((uint64_t)val) << shift;
	return storew(v, addr & ~7ull, orig);
}

static int push(vm_t *v, uint64_t val) {
	assert(v);
	const uint64_t t = v->tos;
	v->tos = val;
	const uint64_t loc = v->sp;
	v->sp -= sizeof (uint64_t);
	return storew(v, loc, t);
}

static int pop(vm_t *v, uint64_t *val) {
	assert(v);
	assert(val);
	*val = v->tos;
	v->sp += sizeof (uint64_t);
	return loadw(v, v->sp, &v->tos, READ);
}

static int cpu(vm_t *v) {
	assert(v);
	uint64_t instr = 0, npc = v->pc + sizeof(uint64_t);
	if (loadw(v, v->pc, &instr, EXECUTE))
		return 1;

	const uint16_t op = instr >> (64 - 16);
	const uint64_t op1 = instr & 0x0000FFFFFFFFFFFFull;
	uint64_t a = v->tos, b = op1, c = 0;
	if (trace(v, "+pc,%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",%"PRIx64",", v->pc, instr, v->sp, v->tos, op1) < 0)
		return -1;
	if ((op & 0x0800) && !bit_get(v->flags, V))
		goto next;
	if ((op & 0x0400) && !bit_get(v->flags, C))
		goto next;
	if ((op & 0x0200) && !bit_get(v->flags, Z))
		goto next;
	if ((op & 0x0100) && !bit_get(v->flags, N))
		goto next;
	if (op & 0x0040)
		if (pop(v, &a))
			return 1;
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
			if (push(v, npc))
				return 1;
		v->pc = b;
		return 0; /* !! */
	}
	switch (op & 63) {
	/* Arithmetic */
	case  0: c = a; break;
	case  1: c = b; break;
	case  2: c = ~a; break;
	case  3: c = a & b; break;
	case  4: c = a | b; break;
	case  5: c = a ^ b; break;
	case  6: c = a << b; break;
	case  7: c = a >> b; break;
	case  8: c = a * b; break;
	case  9: if (!b) return trap(v, T_DIV0, v->pc); c = a / b; break;
	case 10: a += bit_get(v->flags, C); /* fall-through */
	case 11: c = a + b; bit_cnd(&v->flags, C, c < a); bit_cnd(&v->flags, V, ((c ^ a) & (c ^ b)) >> 63); break;
	case 12: a -= bit_get(v->flags, C); /* fall-through */
	case 13: c = a - b; bit_cnd(&v->flags, C, c > a); bit_cnd(&v->flags, V, ((c ^ a) & (c ^ b)) >> 63); break;
	/* Registers */
	case 32: c = v->pc; break;
	case 33: npc = b; break;
	case 34: c = v->sp; break;
	case 35: v->sp = b; break;
	case 36: c = v->flags; break;
	case 37: 
		if (bit_get(v->flags, PRIV) == 0) { 
			v->flags &= 0xFFull << 60;
			v->flags |= (b & ~(0xFFull << 60));
			break;
		}
		v->flags = b;
		break;
	case 38: c = v->level; break;
	case 39:
		 if (bit_get(v->flags, PRIV) == 0)
			 return trap(v, T_PRIV, v->pc);
		 v->level = b;
		 break;
	case 40: c = v->signal; break;
	case 41: v->signal = c; break;
	/* Load/Store */
	case 48: if (loadw(v, b, &c, READ)) return 1; break;
	case 49: if (storew(v, b, a)) return 1; break;
	case 50: { uint8_t cb = 0; if (loadb(v, b, &cb)) return 1; c = cb; } break;
	case 51: if (storeb(v, b, a)) return 1; break;
	/* Misc */
	case 52: return trap(v, b, a);
	case 53: if (tlb_flush_single(v, b, &c)) return 1; break;
	case 54: if (tlb_flush_all(v)) return 1; break;
	case 55: {
		if (bit_get(v->flags, PRIV) == 0) 
			return trap(v, T_PRIV, v->pc);
		const int va = bit_get(a, 15);
		bit_clr(&a, 15);
		if (a > NELEMS(v->tlb_va))
			return trap(v, T_INST, v->pc);
		if (va) v->tlb_va[a] = b; else v->tlb_pa[a] = b;
		}
		break;
	/* NB. Could add floating point operations here */
	default: return trap(v, T_INST, v->pc);
	}

	v->tos = c;
	if (op & 0x2000)
		if (push(v, c))
			return 1;
	if (c == 0)
		bit_set(&v->flags, Z);
	if ((c & (1ull << 63)))
		bit_set(&v->flags, N);
next:
	v->pc = npc;
	return 0;
}

static int interrupt(vm_t *v) {
	assert(v);
	if (v->timer && v->tick >= v->timer) {
		v->tick = 0;
		if (bit_get(v->flags, INTR) == 0)
			return trap(v, T_TIMER, v->timer);
	}
	v->tick++;
	return 0;
}

static int run(vm_t *v, uint64_t step) {
	assert(v);
	int forever = step == 0;
	for (uint64_t i = 0; (i < step || forever) && !v->halt; i++) {
		if (interrupt(v) < 0)
			return -1;
		if (cpu(v) < 0)
			return -1;
	}
	return v->halt;
}

int main(int argc, char **argv) {
	static vm_t v = { .pc = MEMORY_START, .sp = MEMORY_START + PAGE_SIZE, };
	v.trace = stderr;
	if (argc != 3)
		return 1;
	FILE *fin = fopen(argv[1], "rb");
	if (!fin)
		return 2;
	v.loaded = fread(v.m, 1, sizeof v.m, fin);
	if (fclose(fin) < 0)
		return 3;
	if (run(&v, 0) < 0)
		return 4;
	FILE *fout = fopen(argv[2], "wb");
	if (!fout)
		return 5;
	(void)fwrite(v.m, 1, sizeof v.m, fout);
	if (fclose(fout) < 0)
		return 6;
	return 0;
}
