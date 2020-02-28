/* Program: Virtual Machine
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/vm> 
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
 * - We could add our own eForth BIOS, either way we need to load
 *   a simple bootloader.
 * - Each peripheral should exist in its own page with a pointer to
 *   the next peripheral and an identifier field.
 * - Peripherals needed; Info table, Interrupt/Trap handler, Memory Management
 *   Unit (MMU), Timer, Real Time Clock (RTC), UART, Network Interface, 
 *   Keyboard, Mouse, VGA/Screen. The MMU should be MIPs like, it seems
 *   simple to implement.
 * - To get the system up and running we can start with more unrealistic
 *   peripherals then move to some more implementable, for example the
 *   mass storage could initially just be 'load/store page to disk', then
 *   once the system is up we could turn it into 'SPI with CSI Flash or
 *   an SD Card'.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#define SIZE      (8ull*1024ull*1024ull)
#define DISK      (16ul*1024ull*1024ull)
#define START     (SIZE / 2ull)
#define TRAPS     (256)
#define UNUSED(X) ((void)(X))

typedef struct {
	uint64_t vectors[TRAPS];
	uint64_t timer_cycles;
} vm_peripherals_t;

typedef struct {
	uint64_t m[SIZE];
	uint64_t r[16];
	uint64_t h, ipc;
	uint64_t pc;
	vm_peripherals_t p;
	unsigned Z:1, N:1, C:1, V:1;
	unsigned IE:1, PRIV:1, REAL:1;
} vm_t;

static int update(vm_t *v) {
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

static uint64_t loadw(vm_t *v, uint64_t address) {
	assert(v);
	UNUSED(address);
	/* TODO: Real Mode/MMU/TRAPs */
	return 0;
}

static uint8_t loadb(vm_t *v, uint64_t address) {
	assert(v);
	UNUSED(address);
	/* TODO: Real Mode/MMU/TRAPs */
	return 0;
}

static void storew(vm_t *v, uint64_t address, uint64_t value) {
	assert(v);
	UNUSED(address);
	UNUSED(value);
	/* TODO: Real Mode/MMU/TRAPs */
}

static void storeb(vm_t *v, uint64_t address, uint8_t value) {
	assert(v);
	UNUSED(address);
	UNUSED(value);
	/* TODO: Real Mode/MMU/TRAPs */
}

static uint64_t fp_add(uint64_t x, uint64_t y, int u, int v) {
	UNUSED(x);
	UNUSED(y);
	UNUSED(u);
	UNUSED(v);
	return 0;
}

static uint64_t fp_mul(uint64_t x, uint64_t y) {
	UNUSED(x);
	UNUSED(y);
	return 0;
}

static uint64_t fp_div(vm_t *v, uint64_t x, uint64_t y) {
	assert(v);
	UNUSED(x);
	UNUSED(y);
	/* TODO: raise trap if y == 0 */
	return 0;
}

typedef struct { uint64_t quot, rem; } idiv_t;

static idiv_t idiv(vm_t *v, uint64_t x, uint64_t y, int is_signed_div) {
	assert(v);
	UNUSED(x);
	UNUSED(y);
	UNUSED(is_signed_div);
	/* TODO: raise trap if y == 0 */
	return (idiv_t){ 0, 0 };
}

/* <https://stackoverflow.com/questions/25095741> */
static inline void mult64to128(uint64_t op1, uint64_t op2, uint64_t *hi, uint64_t *lo) {
    const uint64_t u1 = (op1 & 0xFFFFFFFFull);
    const uint64_t v1 = (op2 & 0xFFFFFFFFul);
    uint64_t t = u1 * v1;
    const uint64_t w3 = t & 0xFFFFFFFFull;
    uint64_t k = t >> 32;

    op1 >>= 32;
    t = (op1 * v1) + k;
    k = (t & 0xFFFFFFFFull);
    uint64_t w1 = (t >> 32);

    op2 >>= 32;
    t = (u1 * op2) + k;
    k = (t >> 32);

    *hi = (op1 * op2) + w1 + k;
    *lo = (t << 32) + w3;
}

inline uint64_t arshift(const uint64_t v, const unsigned p) {
	if ((v == 0) || !(v & 0x8000000000000000ull))
		return v >> p;
	const uint64_t leading = ((uint64_t)(-1l)) << ((sizeof(v)*CHAR_BIT) - p - 1);
	return leading | (v >> p);
}

static int step(vm_t *v) {
	assert(v);
	if (update(v) < 0)
		return -1;

	enum { /* ANN is merged into AND using ubit */
		MOV, TRP, LSL, ASR, ROR, AND, /*ANN,*/ IOR, XOR,
		ADD, SUB, MUL, DIV,FAD, FSB, FML, FDV,
	};

	const uint64_t pbit = 0x8000000000000000ull;
	const uint64_t qbit = 0x4000000000000000ull;
	const uint64_t ubit = 0x2000000000000000ull;
	const uint64_t vbit = 0x1000000000000000ull;

	uint64_t ir = loadw(v, v->pc / 8ull);
	v->pc++;

	if ((ir & pbit) == 0) { /* ALU instructions */
		uint64_t a  = (ir & 0x0F00000000000000ul) >> 56;
		uint64_t b  = (ir & 0x00F0000000000000ul) >> 52;
		uint64_t op = (ir & 0x000F000000000000ul) >> 48;
		uint64_t im =  ir & 0x0000FFFFFFFFFFFFul;
		uint64_t c  =  ir & 0x000000000000000Ful;
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
		case TRP:
			v->PRIV = 1; /* TODO: Find a way to set/clear PRIV */
			v->IE   = 0; /* TODO: Find a way to allow setting of IE and REAL bit, when PRIV is 1, could use ubit */
			v->ipc  = v->pc & ((1ull << 48) - 1ull);
			v->pc   = v->p.vectors[b_val % TRAPS]; 
			break;
		case LSL: a_val = b_val << (c_val & 63); break;
		case ASR: a_val = arshift(b_val, c_val & 63); break;
		case ROR: a_val = (b_val >> (c_val & 63)) | (b_val << (-c_val & 63)); break;
		case AND: a_val = b_val & ((ir & ubit) != 0) ? c_val : ~c_val; break;
		case IOR: a_val = b_val | c_val; break;
		case XOR: a_val = b_val ^ c_val; break;
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
		case MUL: /* TODO: implement */ break;
		case DIV: /* TODO: implement */ break;
		case FAD: a_val = fp_add(b_val, c_val, !!(ir & ubit), !!(ir & vbit)); break;
		case FSB: a_val = fp_add(b_val, c_val ^ 0x8000000000000000ull, !!(ir & ubit), !!(ir & vbit)); break;
		case FML: a_val = fp_mul(b_val, c_val); break;
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
			const uint64_t a_val = ((ir & vbit) == 0) ? loadw(v, address) : loadb(v, address);
			sreg(v, a, a_val);
		} else {
			if ((ir & vbit) == 0)
				storew(v, address, v->r[a]);
			else
				storeb(v, address, v->r[a]);
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
}

static int run(vm_t *v, unsigned cycles) {
	assert(v);
	for (unsigned c = 0; !cycles || c < cycles; c++) {
		/* debug(v, file) */
		if (step(v) < 0)
			return -1;
	}
	return 0;
}

static int load(char *file, uint64_t *m, size_t length) {
	assert(file);
	assert(m);
	if (length == 0)
		return 0;
	FILE *in = fopen(file, "rb");
	if (!in)
		return -1;
	for (size_t i = 0; i < length; i++) {
		uint8_t b[sizeof *m];
		const size_t rd = fread(b, 1, sizeof b, in);
		if (rd == 0)
			break;
		for (size_t j = 0; j < rd; j++)
			m[i] = b[j] << (CHAR_BIT * j);
	}
	return 0;
}

int main(int argc, char **argv) {
	/* args: kernel disk-image */
	static vm_t v;

	if (argc != 3) {
		fprintf(stderr, "%s kernel disk-image\n", argv[0]);
		return 1;
	}
	load(argv[1], &v.m[START], SIZE - START);
	run(&v, 0);
	/* save */
	return 0;
}
