/* Program: 64-bit Project Oberon-Like Virtual Machine
 * Author:  Richard James Howe (with code taken from Peter De Wachter)
 * Email:   howe.r.j.89@gmail.com
 * License: BSD (Zero clause)
 * Repo:    <https://github.com/howerj/vm> 
 *
 * Copyright (c) 2014 Peter De Wachter, 2020 Richard James Howe
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * The floating point code and a lot the structure for the virtual
 * machine have been taken from:
 *
 * <https://github.com/pdewacht/oberon-risc-emu/>
 *
 * Which is a really clear and neat implementation of the Project
 * Oberon computer as a virtual machine written in C.
 *
 * TODO: Implement a 64-bit version of the Project Oberon instruction
 * set with the additions of a trap mechanism, an MMU, better hardware
 * peripherals (networking could use PCAP, there should be standard
 * ways of identifying peripherals and finding them).
 *
 * The instruction set should be extended with a trap instruction, and
 * perhaps something like an atomic compare and swap instruction.
 *
 * TODO:
 * - Implement 64-bit floating point numbers, 128 bit multiplication, ...
 * - We could add our own eForth BIOS, either way we need to load
 *   a simple bootloader that loads a kernel or bootsector from disk
 *   if present.
 * - Each peripheral should exist in its own page with a pointer to
 *   the next peripheral (a CAR and CDR pointer) and an identifier field
 *   that identifies the peripheral.
 * - Peripherals needed; Info table, Interrupt/Trap handler, Memory Management
 *   Unit (MMU), Timer, Real Time Clock (RTC), UART, Network Interface, 
 *   Keyboard, Mouse, VGA/Screen. The MMU should be MIPs like, it seems
 *   simple to implement.
 * - Add an interactive debugger, logging, and getopts options.
 * - To get the system up and running we can start with more unrealistic
 *   peripherals then move to some more implementable, for example the
 *   mass storage could initially just be 'load/store page to disk', then
 *   once the system is up we could turn it into 'SPI with CSI Flash or
 *   an SD Card'.
 * - Allow memory size and disk size to be set via a command line option.
 * - MIPs MMU or an automatic one like in x86?
 *   Also see <https://wiki.osdev.org/Memory_Management_Unit>
 *   Some MMU/TLB related instructions are needed, at least one to
 *   invalidate a page.
 * - Just a note, a cool instruction would be an 'interpret' instruction,
 *   capable of executing an instruction from a register.
 */
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#define UNUSED(X)             ((void)(X))

#define SIZE                  (1ull*1024ull*1024ull)
#define PAGE                  (4096ull)
#define DISK                  (2ull*1024ull*1024ull)
#define MEMORY_ADDR           (0x0000800000000000ull)
#define IO_ADDR               (0x0000400000000000ull)
#define TRAPS                 (256)

#define TLB_SIZE              (64)

#define TLB_ENTRY_FLAGS_INUSE (63) /* Entry in use */
#define TLB_ENTRY_FLAGS_DIRTY (62)
#define TLB_ENTRY_FLAGS_PRIVL (61) /* Privilege flag */
#define TLB_ENTRY_FLAGS_WXORX (60) /* Write or Execute */
#define TLB_ENTRY_FLAGS_READ  (59) /* Read permission */

#ifdef _WIN32 /* Used to unfuck file mode for "Win"dows. Text mode is for losers. */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static void binary(FILE *f) { _setmode(_fileno(f), _O_BINARY); } /* only platform specific code... */
#else
static inline void binary(FILE *f) { UNUSED(f); }
#endif

enum {
	TRAP_RESET,
	TRAP_ADDR,
	TRAP_DIV0,
	TRAP_FDIV0,
	TRAP_UNALIGNED,
	TRAP_DISABLED,
};

struct vm;
typedef struct vm vm_t;

typedef struct {
	FILE *in, *out;
	int on, init;
} debug_t;

typedef struct {
	char *arg;   /* parsed argument */
	int error,   /* turn error reporting on/off */
	    index,   /* index into argument list */
	    option,  /* parsed option */
	    reset;   /* set to reset */
	char *place; /* internal use: scanner position */
	int  init;   /* internal use: initialized or not */
} vm_getopt_t;       /* getopt clone; with a few modifications */

typedef struct {
   uint64_t vaddr; /* virtual address to lookup */
   uint64_t paddr; /* physical address */
   uint64_t flags; /* could just use upper bits of entry */
} vm_tlb_t; /* an entry in the Translation Look-aside Buffer for the Memory Management Unit */

typedef struct {
	const char *name;
	void *cb_param;
	uint64_t paddr_hi, paddr_lo;
	int (*open)(vm_t *v, void **cb_param);
	int (*close)(vm_t *v, void *cb_param);
	int (*update)(vm_t *v, void *cb_param);
	int (*load)(vm_t *v, void *cb_param, uint64_t *paddr, uint64_t *out);
	int (*store)(vm_t *v, void *cb_param, uint64_t *paddr, uint64_t in);
} vm_peripheral_t;

typedef struct {
	uint64_t vectors[TRAPS];
	uint64_t timer_cycles;
	uint64_t disk[DISK];
} vm_peripherals_t;

struct vm {
	vm_tlb_t tlb[TLB_SIZE];
	vm_peripherals_t p;
	uint64_t m[SIZE];
	uint64_t r[16];
	uint64_t h, ipc;
	uint64_t pc;
	unsigned long cycles;
	unsigned Z:1, N:1, C:1, V:1;
	unsigned IE:1, PRIV:1, REAL:1;
};

static inline int within(uint64_t value, uint64_t lo, uint64_t hi) {
	return (value >= lo) && (value <= hi);
}

static int io_update(vm_t *v) {
	assert(v);
	v->p.timer_cycles++;
	return 0;
}

static void sreg(vm_t *v, unsigned reg, uint64_t value) {
	assert(v);
	v->r[reg] = value;
	v->Z = value == 0;
	v->N = value & 0x8000000000000000ull;
}

static int trap(vm_t *v, unsigned number) {
	assert(v);
	UNUSED(number);
	v->PRIV = 1; /* TODO: Find a way to set/clear PRIV */
	v->IE   = 0; /* TODO: Find a way to allow setting of IE and REAL bit, when PRIV is 1, could use ubit */
	v->ipc  = (v->pc & ((1ull << 48) - 1ull)) * 8ull;
	v->pc   = v->p.vectors[number % TRAPS] / 8ull; 
	return -1;
}

static int tlb_lookup(vm_t *v, uint64_t va, uint64_t *pa) {
	assert(pa);
	*pa = 0;
	/* TODO: privilege level, mask off (PAGE_SIZE - 1) when doing check,
	 * auto lookup when not in TLB... */
	for (size_t i = 0; i < TLB_SIZE; i++)
		if (v->tlb[i].flags & TLB_ENTRY_FLAGS_INUSE && v->tlb[i].vaddr == va) {
			*pa = v->tlb[i].paddr;
			return 1;
		}
	return 0;
}

static int tlb_flush_single(vm_t *v, uint64_t va) {
	assert(v);
	for (size_t i = 0; i< TLB_SIZE; i++)
		if (v->tlb[i].flags & TLB_ENTRY_FLAGS_INUSE && v->tlb[i].vaddr == va) {
			v->tlb[i].flags &= ~TLB_ENTRY_FLAGS_INUSE;
			return 1;
		}
	return 0;
}

static int loadio(vm_t *v, uint64_t address, uint64_t *value) {
	assert(v);
	assert(value);
	assert(within(address, IO_ADDR, MEMORY_ADDR - 8));

	if (within(address, IO_ADDR, IO_ADDR + (1ull * PAGE) - 8ull)) { /* INFO */
	} else if(address < IO_ADDR + (2ull * PAGE)) { /* TRAPS */
	} else if(address < IO_ADDR + (3ull * PAGE)) { /* MMU */
	} else if(address < IO_ADDR + (4ull * PAGE)) { /* TIMER */
	} else if(address < IO_ADDR + (5ull * PAGE)) { /* UART */
	} else if(address < IO_ADDR + (6ull * PAGE)) { /* NETWORK */
	} else if(address < IO_ADDR + (7ull * PAGE)) { /* MOUSE */
	} else if(address < IO_ADDR + (8ull * PAGE)) { /* KEYBOARD */
	} else if(address < IO_ADDR + (9ull * PAGE)) { /* VIDEO */
	}

	return trap(v, TRAP_ADDR);
}

static int storeio(vm_t *v, uint64_t address, uint64_t value) {
	assert(v);
	if (within(address, IO_ADDR, IO_ADDR + (1ull * PAGE) - 8ull)) { /* INFO */
		return trap(v, TRAP_ADDR); /* read only peripheral */
	} else if(address < IO_ADDR + (2ull * PAGE)) { /* TRAPS */
	} else if(address < IO_ADDR + (3ull * PAGE)) { /* MMU */
	} else if(address < IO_ADDR + (4ull * PAGE)) { /* TIMER */
	} else if(address < IO_ADDR + (5ull * PAGE)) { /* UART */
	} else if(address < IO_ADDR + (6ull * PAGE)) { /* NETWORK */
	} else if(address < IO_ADDR + (7ull * PAGE)) { /* MOUSE */
	} else if(address < IO_ADDR + (8ull * PAGE)) { /* KEYBOARD */
	} else if(address < IO_ADDR + (9ull * PAGE)) { /* VIDEO */
	}
	return trap(v, TRAP_ADDR);
}

static int loadw(vm_t *v, uint64_t address, uint64_t *value) {
	assert(v);
	assert(value);
	*value = 0;
	if (address & 7ull)
		return trap(v, TRAP_UNALIGNED);

	if (v->REAL) {
		if (address >= MEMORY_ADDR) {
			if (address >= MEMORY_ADDR + SIZE)
				return trap(v, TRAP_ADDR);
			*value = v->m[(address - MEMORY_ADDR)/8ull];
			return 0;
		} else if (address > IO_ADDR) {
			return loadio(v, address, value);
		} 
		return trap(v, TRAP_ADDR);
	}

	return trap(v, TRAP_ADDR);
}

static int loadb(vm_t *v, uint64_t address, uint8_t *value) {
	assert(v);
	assert(value);
	*value = 0;
	uint64_t vb = 0;
	int st = loadw(v, address & ~7ull, &vb);
	if (st)
		return st;
	*value = (vb >> (address % 8ull * CHAR_BIT));
	return 0;
}

static int storew(vm_t *v, uint64_t address, uint64_t value) {
	assert(v);
	
	if (address & 7ull)
		return trap(v, TRAP_UNALIGNED);

	if (v->REAL) {
		if (address >= MEMORY_ADDR) {
			if (address > MEMORY_ADDR + SIZE)
				return trap(v, TRAP_ADDR);
			v->m[(address - MEMORY_ADDR)/8ull] = value;
			return 0;
		} else if (address > IO_ADDR) {
			return storeio(v, address, value);
		} else {
			return trap(v, TRAP_ADDR);
		}
	}

	/* TODO: Real Mode/MMU/TRAPs */
	return trap(v, TRAP_ADDR);
}

static int storeb(vm_t *v, uint64_t address, uint8_t value) {
	assert(v);
	UNUSED(address);
	UNUSED(value);
	uint64_t vw = 0;
	int st = loadw(v, address & (~7ull), &vw);
	const unsigned shift = (address & 7ull) * CHAR_BIT;
	if (st)
		return -1;
	vw &= ~(0xFFull << shift);
	vw |=  ((uint64_t)value) << shift;
	return storew(v, address & (~7ull), vw);
}

static inline uint64_t arshift64(const uint64_t v, const unsigned p) {
	if ((v == 0) || !(v & 0x8000000000000000ull))
		return v >> p;
	const uint64_t leading = ((uint64_t)(-1l)) << ((sizeof(v)*CHAR_BIT) - p - 1);
	return leading | (v >> p);
}

static inline uint32_t arshift32(const uint32_t v, const unsigned p) {
	if ((v == 0) || !(v & 0x80000000ul))
		return v >> p;
	const uint32_t leading = ((uint32_t)(-1l)) << ((sizeof(v)*CHAR_BIT) - p - 1);
	return leading | (v >> p);
}

static inline void reverse(char * const r, const size_t length) {
	const size_t last = length - 1;
	for (size_t i = 0; i < length/2ul; i++) {
		const size_t t = r[i];
		r[i] = r[last - i];
		r[last - i] = t;
	}
}

static uint64_t fp_add(vm_t *v, uint64_t x, uint64_t y, int ubit, int vbit) {
	assert(v);
	int xs = (x & 0x80000000ul) != 0;
	uint32_t xe = 0;
	int32_t x0 = 0;
	if (!ubit) {
		xe = (x >> 23) & 0xFF;
		uint32_t xm = ((x & 0x7FFFFFul) << 1) | 0x1000000ul;
		x0 = (int32_t)(xs ? -xm : xm);
	} else {
		xe = 150;
		x0 = (int32_t)(x & 0x00FFFFFFul) << 8 >> 7; /* TODO: Not portable */
	}

	int ys = (y & 0x80000000ul) != 0;
	uint32_t ye = (y >> 23) & 0xFFul;
	uint32_t ym = ((y & 0x7FFFFFul) << 1);
	if (!ubit && !vbit) 
		ym |= 0x1000000ul;
	const int32_t y0 = (int32_t)(ys ? -ym : ym);

	uint32_t e0 = 0;
	int32_t x3 = 0, y3 = 0;
	if (ye > xe) {
		const uint32_t shift = ye - xe;
		e0 = ye;
		/*x3 = shift > 31 ? x0 >> 31 : x0 >> shift;*/
		x3 = shift > 31 ? (int32_t)arshift32(x0, 31) : x0 >> shift;
		y3 = y0;
	} else {
		const uint32_t shift = xe - ye;
		e0 = xe;
		x3 = x0;
		/*y3 = shift > 31 ? y0 >> 31 : y0 >> shift;*/
		y3 = shift > 31 ? (int32_t)arshift32(y0, 31) : y0 >> shift;
	}

	uint32_t sum = ((xs << 26) | (xs << 25) | (x3 & 0x01FFFFFF))
	+ ((ys << 26) | (ys << 25) | (y3 & 0x01FFFFFF));

	uint32_t s = (((sum & (1 << 26)) ? -sum : sum) + 1) & 0x07FFFFFF;

	uint32_t e1 = e0 + 1;
	uint32_t t3 = s >> 1;
	if ((s & 0x3FFFFFCul) != 0) {
		while ((t3 & (1ul << 24)) == 0) {
			t3 <<= 1;
			e1--;
		}
	} else {
		t3 <<= 24;
		e1 -= 24;
	}

	int xn = (x & 0x7FFFFFFFul) == 0;
	int yn = (y & 0x7FFFFFFFul) == 0;

	if (vbit)
		return (int32_t)(sum << 5) >> 6; /* TODO: Not portable! */
	else if (xn)
		return (ubit | yn) ? 0 : y;
	else if (yn)
		return x;
	else if ((t3 & 0x01FFFFFFul) == 0 || (e1 & 0x100ul) != 0)
		return 0;
	return ((sum & 0x04000000ul) << 5) | (e1 << 23) | ((t3 >> 1) & 0x7FFFFFul);
}

static uint64_t fp_mul(vm_t *v, uint64_t x, uint64_t y) {
	assert(v);
	const uint32_t sign = (x ^ y) & 0x80000000ul;
	const uint32_t xe = (x >> 23) & 0xFFul;
	const uint32_t ye = (y >> 23) & 0xFFul;

	const uint32_t xm = (x & 0x7FFFFFul) | 0x800000ul;
	const uint32_t ym = (y & 0x7FFFFFul) | 0x800000ul;
	const uint64_t m = (uint64_t)xm * ym;

	uint32_t e1 = (xe + ye) - 127ul;
	uint32_t z0 = 0;
	if ((m & (1ull << 47)) != 0) {
		e1++;
		z0 = ((m >> 23) + 1) & 0xFFFFFFul;
	} else {
		z0 = ((m >> 22) + 1) & 0xFFFFFFul;
	}

	if (xe == 0 || ye == 0)
		return 0;
	else if ((e1 & 0x100) == 0)
		return sign | ((e1 & 0xFF) << 23) | (z0 >> 1);
	else if ((e1 & 0x80) == 0)
		return sign | (0xFF << 23) | (z0 >> 1);
	return 0;
}

static uint64_t fp_div(vm_t *v, uint64_t x, uint64_t y) {
	assert(v);
	UNUSED(x);
	if (y == 0)
		trap(v, TRAP_FDIV0);
	const uint32_t sign = (x ^ y) & 0x80000000ul;
	const uint32_t xe = (x >> 23) & 0xFFul;
	const uint32_t ye = (y >> 23) & 0xFFul;

	uint32_t xm = (x & 0x7FFFFFul) | 0x800000ul;
	uint32_t ym = (y & 0x7FFFFFul) | 0x800000ul;
	uint32_t q1 = (xm * (1ull << 25) / ym);

	uint32_t e1 = (xe - ye) + 126;
	uint32_t q2 = 0;
	if ((q1 & (1 << 25)) != 0) {
		e1++;
		q2 = (q1 >> 1) & 0xFFFFFF;
	} else {
		q2 = q1 & 0xFFFFFF;
	}
	uint32_t q3 = q2 + 1ul;

	if (xe == 0)
		return 0;
	else if (ye == 0)
		return sign | (0xFF << 23);
	else if ((e1 & 0x100) == 0)
		return sign | ((e1 & 0xFF) << 23) | (q3 >> 1);
	else if ((e1 & 0x80) == 0)
		return sign | (0xFF << 23) | (q2 >> 1);
	return 0;
}

typedef struct { uint64_t quot, rem; } idiv_t;

static idiv_t divide(vm_t *v, uint64_t x, uint64_t y, int is_signed_div) {
	assert(v);
	UNUSED(x);
	UNUSED(is_signed_div);
	if (y == 0)
		trap(v, TRAP_DIV0);
	return (idiv_t){ 0, 0 };
}

/* <https://stackoverflow.com/questions/25095741> */
static inline void multiply(uint64_t op1, uint64_t op2, uint64_t *hi, uint64_t *lo) {
    const uint64_t u1 = (op1 & 0xFFFFFFFFull);
    const uint64_t v1 = (op2 & 0xFFFFFFFFull);
    uint64_t t = u1 * v1;
    const uint64_t w3 = t & 0xFFFFFFFFull;
    uint64_t k = t >> 32;

    op1 >>= 32;
    t = (op1 * v1) + k;
    k = (t & 0xFFFFFFFFull);
    uint64_t w1 = t >> 32;

    op2 >>= 32;
    t = (u1 * op2) + k;
    k = t >> 32;

    *hi = (op1 * op2) + w1 + k;
    *lo = (t << 32) + w3;
}


static inline void u64_to_csv(char b[64], uint64_t u, const char delimiter, const uint64_t base) {
	assert(b);
	assert(within(base, 2, 16));
	unsigned i = 0;
	do {
		const uint64_t q = u % base;
		const uint64_t r = u / base;
		b[i++] = q["0123456789ABCDEF"];
		u = r;
	} while (u);
	b[i] = delimiter;
	b[i+1] = '\0';
	reverse(b, i);
}

static inline int print_value(FILE *o, const uint64_t u) {
	assert(o);
	char b[64] = { 0 };
	u64_to_csv(b, u, ',', 16);
	return fputs(b, o);
}

static int debug(vm_t *v, debug_t *d, uint64_t st) {
	assert(v);
	assert(d);
	if (!(d->on))
		return 0;
	if (!(d->init)) {
		if (fprintf(d->out, "pc,st,flags,") < 0)
			return -1;
		for (size_t i = 0; i < 16; i++)
			if (fprintf(d->out, "r%d,", (int)i) < 0)
				return -1;
		if (fputc('\n', d->out) < 0)
			return -1;
		d->init = 1;
	}

	if (print_value(d->out, v->pc * 8ull) < 0)
		return -1;
	if (print_value(d->out, st) < 0)
		return -1;
	const uint64_t flags = ((uint64_t)v->N    << 63) |
			((uint64_t)v->Z    << 62) |
			((uint64_t)v->C    << 61) |
			((uint64_t)v->V    << 60) |
			((uint64_t)v->IE   << 59) |
			((uint64_t)v->PRIV << 58) |
			((uint64_t)v->REAL << 57) |
			(v->ipc & ((1ull << 48) - 1ull));
	if (print_value(d->out, flags) < 0)
		return -1;
	for (size_t i = 0; i < 16; i++)
		if (print_value(d->out, v->r[i]) < 0)
			return -1;
	return fputc('\n', d->out) < 0 ? -1 : 0;
}

static int step(vm_t *v, debug_t *d) { /* returns: 0 == ok, 1 = trap, -1 = simulation error */
	assert(v);
	assert(d);
	if (io_update(v) < 0)
		goto nop;

	enum { /* ANN is merged into AND using ubit, as with IOR/XOR  */
		MOV, TRP, TLB, LSL, ASR, ROR, AND, /*ANN,*/ IOR, /*XOR,*/
		ADD, SUB, MUL, DIV,FAD, FSB, FML, FDV,
	};

	const uint64_t pbit = 0x8000000000000000ull;
	const uint64_t qbit = 0x4000000000000000ull;
	const uint64_t ubit = 0x2000000000000000ull;
	const uint64_t vbit = 0x1000000000000000ull;

	uint64_t ir = 0;
	int st = loadw(v, v->pc * 8ull, &ir);

	if (debug(v, d, ir) < 0)
		return -1;

	if (st == 0)
		v->pc++;
	else
		goto nop;

	if ((ir & pbit) == 0) { /* ALU instructions */
		uint64_t a  = (ir & 0x0F00000000000000ul) >> 56;
		uint64_t b  = (ir & 0x00F0000000000000ul) >> 52;
		uint64_t c  = (ir & 0x000000000000000Ful) >>  0;
		uint64_t op = (ir & 0x000F000000000000ul) >> 48;
		uint64_t im = (ir & 0x0000FFFFFFFFFFFFul) >>  0;
		uint64_t a_val = 0, b_val = 0, c_val = 0;

		b_val = v->r[b];
		if ((ir & qbit) == 0)
			c_val = v->r[c];
		else if ((ir & vbit) == 0)
			c_val = im;
		else
			c_val = 0xFFFF000000000000ull | im;

		switch (op) {
		case MOV: 
			if ((ir & ubit) == 0) {
				a_val = c_val;
			} else if ((ir & qbit) != 0) {
				a_val = c_val << 48;
			} else if ((ir & vbit) != 0) {
				a_val = ((uint64_t)v->N    << 63) |
					((uint64_t)v->Z    << 62) |
					((uint64_t)v->C    << 61) |
					((uint64_t)v->V    << 60) |
					((uint64_t)v->IE   << 59) |
					((uint64_t)v->PRIV << 58) |
					((uint64_t)v->REAL << 57) |
					(v->ipc & ((1ull << 48) - 1ull));
			} else {
				a_val = v->h;
			}
			break;
		case TRP: a_val = b_val; trap(v, b_val); break;
		case TLB: a_val = 0; trap(v, TRAP_DISABLED); /* TODO: check privilege level, invalidate, install entry, ... */ break;
		case LSL: a_val = b_val << (c_val & 63); break;
		case ASR: a_val = arshift64(b_val, c_val & 63); break;
		case ROR: a_val = (b_val >> (c_val & 63)) | (b_val << (-c_val & 63)); break;
		case AND: a_val = b_val & ((ir & ubit) != 0) ? c_val : ~c_val; break;
		case IOR: a_val = (ir & ubit) != 0 ? b_val | c_val : b_val ^ c_val; break;
		case ADD: 
			a_val = b_val + c_val;
			if ((ir & ubit) != 0)
				a_val += v->C;
			v->C = a_val < b_val;
			v->V = ((a_val ^ c_val) & (a_val ^ b_val)) >> 63;
			break;
		case SUB: 
			a_val = b_val - c_val;
			if ((ir & ubit) != 0)
				a_val -= v->C;
			v->C = a_val > b_val;
			v->V = ((b_val ^ c_val) & (a_val ^ b_val)) >> 63;
			break;
		case MUL: 
			if ((ir & ubit) == 0) { /* TODO: sign extend 64-bit to 128-bit then multiply */
				trap(v, TRAP_DISABLED);
			} else {
				multiply(b_val, c_val, &v->h, &a_val);
			}
			break;
		case DIV: trap(v, TRAP_DISABLED);/* TODO: implement */ break;
		case FAD: a_val = fp_add(v, b_val, c_val, !!(ir & ubit), !!(ir & vbit)); break;
		case FSB: a_val = fp_add(v, b_val, c_val ^ 0x8000000000000000ull, !!(ir & ubit), !!(ir & vbit)); break;
		case FML: a_val = fp_mul(v, b_val, c_val); break;
		case FDV: a_val = fp_div(v, b_val, c_val); break;
		}
		sreg(v, a, a_val);
	} else if ((ir & qbit) == 0) { /* memory instructions */
		uint64_t a = (ir & 0x0F00000000000000ull) >> 56;
		uint64_t b = (ir & 0x00F0000000000000ull) >> 52;
		uint64_t off = ir & 0x000FFFFFFFFFFFFFull;
		off = (off ^ 0x0080000000000000ull) - 0x0080000000000000ull;  // sign-extend

		uint64_t address = v->r[b] + off;
		if ((ir & ubit) == 0) {
			uint64_t a_val = 0;
			int st = 0;
			if ((ir & vbit) == 0) {
				st = loadw(v, address, &a_val);
			} else {
				uint8_t b = 0;
				st = loadb(v, address, &b);
				a_val = b;
			}
			sreg(v, a, st == 0 ? a_val : 0);
		} else {
			if ((ir & vbit) == 0)
				(void)storew(v, address, v->r[a]);
			else
				(void)storeb(v, address, v->r[a]);
		}
	} else { /* branch */
		unsigned t = (ir >> 59) & 1u;
		switch ((ir >> 56) & 7u) {
		case 0u: t ^= v->N; break;
		case 1u: t ^= v->Z; break;
		case 2u: t ^= v->C; break;
		case 3u: t ^= v->V; break;
		case 4u: t ^= v->C | v->Z; break;
		case 5u: t ^= v->N ^ v->V; break;
		case 6u: t ^= (v->N ^ v->V) | v->Z; break;
		case 7u: t ^= 1; break;
		}
		if (t) {
			if ((ir & vbit) != 0)
				sreg(v, 15, v->pc * 8ull);

			if ((ir & ubit) == 0) {
				uint64_t c = ir & 0x0000000F00000000ull;
				v->pc = v->r[c] / 8ull;
			} else {
				uint64_t off = ir & 0x00FFFFFFFFFFFFFFull;
				off = (off ^ 0x0080000000000000ull) - 0x0080000000000000ull;
				v->pc = v->pc + off;
			}
		}
	}

	return 0;
nop:
	if (debug(v, d, st) < 0)
		return -1;
	return 1;
}

static int run(vm_t *v, debug_t *d, unsigned long cycles) {
	assert(d);
	assert(v);
	for (unsigned long c = 0; !cycles || c < cycles; c++)
		if (step(v, d) < 0)
			return -1;
	return 0;
}

static int init(vm_t *v) {
	assert(v);
	v->pc = MEMORY_ADDR / 8ull;
	v->REAL = 1;
	v->PRIV = 1;
	v->IE = 0;
	return 0;
}

static int load(char *file, uint64_t *m, size_t length, unsigned is_hex) {
	assert(file);
	assert(m);
	if (length == 0)
		return 0;
	FILE *in = fopen(file, "rb");
	if (!in)
		return -1;
	for (size_t i = 0; i < length; i++)
		if (is_hex) {
			uint64_t value = 0;
			if (fscanf(in, "%" SCNu64, &value) != 1)
				break;
			m[i] = value;
		} else {
			uint8_t b[sizeof *m];
			const size_t rd = fread(b, 1, sizeof b, in);
			if (rd == 0)
				break;
			for (size_t j = 0; j < rd; j++)
				m[i] = b[j] << (CHAR_BIT * j);
		}
	return fclose(in) < 0 ? -1 : 0;
}

/* Adapted from: <https://stackoverflow.com/questions/10404448> */
static int vm_getopt(vm_getopt_t *opt, const int argc, char *const argv[], const char *fmt) {
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

static int help(FILE *output, const char *arg0) {
	assert(output);
	assert(arg0);
	static const char *usage = "\
Usage   : %s\n\
Program : 64-bit Project Oberon-Like virtual machine\n\
Author  : Richard James Howe (with code taken from <https://github.com/pdewacht/oberon-risc-emu/>.\n\
Email   : howe.r.j.89@gmail.com\n\
Repo    : <https://github.com/howerj/vm>\n\
License : BSD (Zero Clause) (2014 Peter De Wachter, 2020 Richard James Howe)\n\
";
	return fprintf(output, usage, arg0);
}

int main(int argc, char **argv) {
	static vm_t v;
	debug_t d = { .in = stdin, .out = stdout, .on = 0 };
	vm_getopt_t opt = { .init = 0 };
	unsigned long cycles = 0, is_hex = 0;

	binary(stdin);
	binary(stdout);
	binary(stderr);

	if (init(&v) < 0)
		return 1;

	static char ibuf[BUFSIZ], obuf[BUFSIZ];
	if (setvbuf(stdin, ibuf, _IOFBF, sizeof ibuf) < 0)
		return 1;
	if (setvbuf(stdout, obuf, _IOFBF, sizeof obuf) < 0)
		return 1;

	for (int ch = 0; (ch = vm_getopt(&opt, argc, argv, "hxDk:d:c:")) != -1; ) {
		switch (ch) {
		case 'h': help(stdout, argv[0]); return 0;
		case 'x': is_hex ^= 1; break;
		case 'D': d.on = 1; break;
		case 'c': cycles = atol(opt.arg); break;
		case 'k':
			if (load(opt.arg, &v.m[0], SIZE, is_hex) < 0) {
				fprintf(stderr, "load kernel failed: %s\n", argv[1]);
				return 1;
			}
			break;
		case 'd':
			if (load(opt.arg, &v.p.disk[0], DISK, is_hex) < 0) {
				fprintf(stderr, "load disk failed: %s\n", argv[2]);
				return 1;
			}
			break;
		default: help(stderr, argv[0]); return 1;
		}
	}

	if (run(&v, &d, cycles) < 0) {
		fprintf(stderr, "run failed\n");
		return 1;
	}
	return 0;
}

