/* Program: Virtual Machine
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/vm>
 *
 * This is a work in progress Virtual Machine for an Operating
 * System project.
 *
 * TODO/NOTES:
 * - Specify instruction set, the instruction set should be
 *   simple and regular, RISCy, not too much
 * - 16 registers, some for stacks, stack frame, zero register
 *   and CPU flag. There could be two registers also used for
 *   comparison to make sure arithmetic does not go out of bounds
 * - Add peripherals for; Info, Interrupts, Timers, MMU, UART {Either
 *   pure C, or OS read/write to terminal functions}, A Real Time Clock,
 *   Networking {libpcap}, Video {libsdl2}, Key board 
 *   {libsdl2}, Mouse {libsdl2}, Audio {libsdl2}. Anything requiring
 *   an external library would be optional.
 * - Each peripheral should have an ID field and a pointer to
 *   the next peripheral in a list of peripherals
 * - On a trap the CPU privilege level should be upgraded
 * - After the basics are done, extensions for accelerating
 *   virtualization should be added
 * - Multicore processing is a lower priority for now
 * - For the disk, we can load the entire thing into memory and
 *   only save changes if needed
 * - Make a simple BIOS (or fake one) that loads a sector off
 *   off disk and executes it. There would be scope for writing
 *   a complete BIOS as a Forth interpreter...but that would be
 *   an extra much later down the line. The simple (or fake BIOS)
 *   would walk the hardware table looking for the first disk and
 *   load 4096 bytes off it.
 * - If the instruction set is not dense enough one of the op-codes
 *   could signal that a more dense encoding should be used, for
 *   example packing 7 8-bit instructions into a single 64-bit word.
 * - Debugging! Either very fast logging and/or an interactive
 *   debugger need make that accepts some primitive commands to
 *   interrogate the state of the system. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#define SIZE  (8ull*1024ull*1024ull)
#define DISK  (16ul*1024ull*1024ull)
#define START (SIZE / 2ull)

enum {
	REG_ZER = 15,
	REG_CPU = 14,
	REG_STK = 13,
};

enum {
	FLG_ZER  =  0,
	FLG_EQL  =  1,
	FLG_LES  =  3,
	FLG_MOR  =  2,
	FLG_OVR  =  4,
	FLG_UND  =  5,
	FLG_NEG  =  6,
	FLG_REL  =  7,
	FLG_PRIV =  8, /* privilege flag   - must be privileged to set / clear */
	FLG_INT  =  9, /* interrupt enable - must be privileged to set / clear */
	FLG_REAL = 10, /* real mode        - must be privileged to set / clear */
};

typedef struct {
	uint64_t vectors[256];
	uint64_t timer_cycles;
} vm_peripherals_t;

typedef struct {
	uint64_t m[SIZE];
	uint64_t r[16];
	uint64_t pc;
	vm_peripherals_t p;
} vm_t;

static int update(vm_t *v) {
	assert(v);
	v->p.timer_cycles++;
	return 0;
}

static int mmu_r(vm_t *v, uint64_t addr, uint64_t *out) {
	assert(v);
	assert(out);
	/* TODO: implement MMU */
	if (addr > SIZE)
		return -1;
	/* if mmu not on just do a normal read */
	*out = v->m[addr/8u];
	return 0;
}

static inline uint64_t cpustat(vm_t *v) {
	assert(v);
	return v->r[REG_CPU];
}

static int mmu_w(vm_t *v, uint64_t addr, uint64_t in) {
	assert(v);
	/* TODO: implement MMU, come up with a memory
	 * map for peripherals, starting memory address,
	 * etcetera. */
	if (addr > SIZE)
		return -1;
	v->m[addr/8u] = in;
	return 0;
}

static inline uint64_t rreg(vm_t *v, unsigned reg) {
	assert(v);
	assert(reg < 16);
	if (reg == REG_ZER) /* zero register */
		return 0;
	return v->r[reg];
}

static inline int wreg(vm_t *v, unsigned reg, uint64_t wr) {
	assert(v);
	assert(reg < 16);
	if (reg == REG_ZER)
		return 0;
	if (reg == REG_CPU) { /* CPU Status - cannot set privilege flag if running unprivileged */
		const uint64_t cpust = v->r[REG_CPU];
		if ((cpust & FLG_PRIV) == 0) {
			if (wr & FLG_PRIV)
				return -1;
		}
	}
	v->r[reg] = wr;
	return 0;
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

static inline int step(vm_t *v) {
	assert(v);
	if (update(v) < 0) {
		/* TRAP */
	}
	uint64_t instr = 0;
	if (mmu_r(v, v->pc, &instr) < 0) {
		/* TRAP */
	}

	/* Jump/Call:    OP {63-56} | Flags {55-48} | Address {47 - 0} 
	 * Literal:      OP {63-56} | Flags {55-48} | Dest Reg | Literal 
	 * Load/Store    OP {63-56} | ??? | Address {47 - 0}
	 * Arithmetic:   OP {63-56} | Flags {55-48} | Dest Reg {47-44} | Source Reg A {43-40} | Literal/Source Reg B */
	const uint8_t opcode = (instr >> 56) & 0x3Full;
	const uint8_t group = instr >> 62;

	/* Add, Sub, Mul?, Jump {Rel, NOUGLEZ}, Call, 
	 * Return, Shift Left, Shift Right, Rotate Left, 
	 * Rotate Right, And, Or, Xor, Xnor, Count Leading Zeros 
	 * (and other bit functions?), Move, Load, Store, Literal
	 * Also see:
	 * https://en.wikipedia.org/wiki/Bit_Manipulation_Instruction_Sets */


	switch (group) {
	case 0: /* Jump/Calls */
	{
		const uint64_t cflags = cpustat(v) & 0x7Full;
		const uint64_t iflags = (instr >> 48) & 0x7Full;
		const uint64_t addrop = instr  & 0x0000FFFFFFFFFFFFull;
		const int is_relative = (instr >> 55) & 0x1ull;
		const uint64_t addres = is_relative && (addrop & 0x0000800000000000ull) ? addrop | 0xFFFF000000000000ull : addrop;
		const int do_the_jump = !!(cflags ^ iflags);
		switch (opcode) {
		case 0: /* JUMP */
			if (do_the_jump)
				v->pc = is_relative ? v->pc + addres : addres;
			goto none;
		case 1: /* CALL */
			if (do_the_jump) {
				v->r[REG_STK] += 8u;
				if (mmu_w(v, v->r[REG_STK], v->pc + 8u) < 0) {
					/* TRAP */
				} else {
					v->pc = is_relative ? v->pc + addres : v->pc;
				}
			}
			goto none;
		default:
			/* TRAP */
			break;
		}
		break;
	}
	case 1: /* Arithmetic */
	{
		const uint64_t cflags = cpustat(v) & 0x7Full;
		const uint64_t iflags = (instr >> 48) & 0x7Full;
		const int is_literal  = (instr >> 55) & 0x1ull;
		const int do_the_inst = !!(cflags ^ iflags);
		const uint8_t dstr = (instr >> 45);
		const uint8_t srca = (instr >> 44) & 0xFull;
		const uint8_t srcb = (instr >> 40) & 0xFull;
		const uint64_t lit = instr & 0x000000FFFFFFFFFFull;
		if (do_the_inst == 0)
			goto increment;
		const uint64_t a   = rreg(v, srca);
		const uint64_t b   = is_literal ? lit : rreg(v, srcb);
		switch (opcode) {
		case 0: { /* ADD */
			const uint64_t r = a + b;
			const int over = (r < a || r < b);

			if (wreg(v, dstr, r) < 0) {
				/* TRAP */
			}
			if (over)
				v->r[FLG_OVR] |= 1ull << FLG_OVR;
			break;
		}
		case 1:
			break;
		}
		break;
	}
	case 2: { /* Load/Store */
		const int is_relative = (instr >> 55) & 0x1ull;
		break;
	}
	case 3:
		break;
	default:
		/* TRAP */
		break;
	}
increment:
	v->pc += 8ull;
none:
	;
	return 0;
}

static int run(vm_t *v, unsigned cycles) {
	assert(v);
	for (unsigned c = 0; !cycles || c < cycles; c++) {
		/* debug(v) */
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

