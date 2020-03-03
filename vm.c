/* Program: 64-bit Project Oberon-Like Virtual Machine
 * Author:  Richard James Howe (with code taken from Peter De Wachter)
 * Email:   howe.r.j.89@gmail.com
 * License: ISC
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
 * - Add tests for the arithmetic functions
 * - It would be nice to be able to specify two page sizes, one big enough
 *   to hold the kernel/most user land utilities, and one for general usage.
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
 *   capable of executing an instruction from a register. */
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#define UNUSED(X)             ((void)(X))
#ifdef _WIN32 /* Used to unfuck file mode for "Win"dows. Text mode is for losers. */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static void binary(FILE *f) { _setmode(_fileno(f), _O_BINARY); } /* only platform specific code... */
#else
static inline void binary(FILE *f) { UNUSED(f); }
#endif

#define RAM_SIZE              (1ull*1024ull*1024ull)
#define PAGE                  (4096ull)
#define PAGE_MASK             (PAGE - 1ull)
#define DISK_SIZE             (2ull*1024ull*1024ull)
#define RAM_ADDR              (0x0000800000000000ull)
#define IO_ADDR               (0x0000400000000000ull)
#define TRAPS                 (256)
#define PERIPHERAL_SLOTS      (12)

#define TLB_SIZE              (64)

enum { READ, WRITE, EXECUTE };

enum {
	TLB_ENTRY_FLAGS_INUSE    = 63, /* Entry in use */
	TLB_ENTRY_FLAGS_PRIVL    = 62, /* Privilege flag */
	TLB_ENTRY_FLAGS_DIRTY    = 61, /* Written to */
	TLB_ENTRY_FLAGS_ACCESSED = 60, /* Page has been accessed */
	TLB_ENTRY_FLAGS_READ     = 59, /* Read permission */
	TLB_ENTRY_FLAGS_WRITE    = 58, /* Write permission */
	TLB_ENTRY_FLAGS_EXECUTE  = 57, /* Execute permission */
};

enum {
	TRAP_RESET,
	TRAP_BUS_ERROR,
	TRAP_UNMAPPED,
	TRAP_UNALIGNED,
	TRAP_PROTECTION,
	TRAP_DISABLED,
	TRAP_DIV0,
	TRAP_FDIV0,
};

enum {
	IO_UNUSED,
	IO_INFO,
	IO_INTERRUPTs,
	IO_MMU,
	IO_TIMER,
	IO_UART,
	IO_DISK,
	IO_NETWORK,
	IO_MOUSE,
	IO_KEYBOARD,
	IO_VIDEO,
	IO_AUDIO,
};

enum { /* register 14 is a special register */
	R14_PRIV    = 63,
	R14_INT_EN  = 62,
	R14_REAL    = 61,
	R14_FAULT   = 60,
	R14_Z       = 59,
	R14_N       = 58,
	R14_C       = 57,
	R14_V       = 56,
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
   uint64_t paddr; /* physical address, flags in top 16 bits */
   uint64_t vaddr; /* virtual address to lookup, flags in top 16 bits not used */
} vm_tlb_t; /* an entry in the Translation Look-aside Buffer for the Memory Management Unit */

struct vm_io;
typedef struct vm_io vm_io_t;

struct vm_io {
	void *cb_param;
	uint64_t id, car, cdr, size; /* each peripheral has an ID field and next pointer */
	uint64_t paddr_hi, paddr_lo;
	int (*open)(vm_io_t *p, vm_t *v, void *resource);
	int (*close)(vm_io_t *p, vm_t *v);
	int (*update)(vm_io_t *p, vm_t *v);
	int (*load)(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t *out);
	int (*store)(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t in);
};

typedef struct {
	uint64_t vectors[TRAPS];
	uint64_t disk[DISK_SIZE];
} vm_ios_t;

struct vm {
	vm_tlb_t tlb[TLB_SIZE];
	vm_ios_t p;
	vm_io_t io[PERIPHERAL_SLOTS];
	uint64_t m[RAM_SIZE];
	uint64_t r[16];
	uint64_t H, IPC;
	uint64_t PC;
	unsigned long cycles;
	unsigned Z:1, N:1, C:1, V:1;
	unsigned IE:1, PRIV:1, REAL:1, FAULT:2;
	/* TODO: Move special register bits to register 14 */
	/* TODO: add mechanism for detecting and clearing faults */
};

typedef struct { 
	uint64_t quot, rem; 
} idiv_t;

enum { BACKSPACE = 8, ESCAPE = 27, DELETE = 127, };

static inline void bit_set(uint64_t *x, const int bit) {
	assert(x);
	*x |= (1ull << bit);
}

static inline void bit_clear(uint64_t * const x, const int bit) {
	assert(x);
	*x &= ~(1ull << bit);
}

static inline int bit_is_set(const uint64_t x, const int bit) {
	return !!(x & (1ull << bit));
}

static inline void bit_copy(uint64_t * const dst, const uint64_t src, const int bit) {
	if (bit_is_set(src, bit))
		bit_set(dst, bit);
	else
		bit_clear(dst, bit);
}

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
static int getch(void) {
	return getchar();
}

static int putch(const int c) {
	return putchar(c);
}
#endif
#endif /** __unix__ **/

static int wrap_getch(/*int *debug_on*/) {
	const int ch = getch();
	//assert(debug_on);
	if (ch == EOF) {
		fprintf(stderr, "End Of Input - exiting\n");
		exit(EXIT_SUCCESS);
	}
	if (ch == 27 /*&& debug_on*/) {
		//*debug_on = 1;
		exit(0);
	}

	return ch == DELETE ? BACKSPACE : ch;
}

static inline int within(uint64_t value, uint64_t lo, uint64_t hi) {
	return (value >= lo) && (value <= hi);
}

static int trap(vm_t *v, unsigned number) {
	assert(v);
	UNUSED(number);
	v->PRIV = 1; /* TODO: Find a way to set/clear PRIV */
	v->IE   = 0; /* TODO: Find a way to allow setting of IE and REAL bit, when PRIV is 1, could use ubit */
	v->IPC  = (v->PC & ((1ull << 48) - 1ull)) * 8ull;
	v->PC   = v->p.vectors[number % TRAPS] / 8ull; 
	return -1;
}

static int io_default_close(vm_io_t *p, vm_t *v) {
	assert(p && v);
	free(p->cb_param);
	p->cb_param = NULL;
	return 0;
}

static int io_default_open(vm_io_t *p, vm_t *v, void *resource) {
	assert(p && v);
	UNUSED(resource);
	if (p->close(p, v) < 0)
		return -1;
	return 0;
}

static int io_default_update(vm_io_t *p, vm_t *v) {
	assert(p && v);
	return 0;
}

static int io_default_load(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t *out) {
	assert(p && v);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	assert(out);
	return trap(v, TRAP_BUS_ERROR);
}

static int io_default_store(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t in) {
	assert(p && v);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	UNUSED(in);
	return trap(v, TRAP_BUS_ERROR);
}

static int io_info_load(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t *out) {
	assert(p && v);
	assert(out);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	paddr -= p->paddr_lo;
	switch (paddr) {
	case 12: *out = 0; break;
	case 16: *out = 0; break;
	case 20: *out = RAM_ADDR; break;
	case 24: *out = RAM_SIZE; break;
	default: return trap(v, TRAP_BUS_ERROR);
	}
	return 0;
}

static int io_timer_open(vm_io_t *p, vm_t *v, void *resource) {
	assert(p && v);
	UNUSED(resource);
	p->cb_param = NULL;
	p->cb_param = calloc(1, sizeof (uint64_t));
	if (!(p->cb_param))
		return -1;
	return 0;
}

static int io_timer_update(vm_io_t *p, vm_t *v) {
	assert(p && v);
	uint64_t *timer = p->cb_param;
	(*timer)++;
	return 0;
}

static int io_timer_load(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t *out) {
	assert(p && v);
	assert(out);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	paddr -= p->paddr_lo;
	switch (paddr) {
	case 12: *out = *(uint64_t*)(p->cb_param); break;
	default: return trap(v, TRAP_BUS_ERROR);
	}
	return 0;
}

static int io_timer_store(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t in) {
	assert(p && v);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	switch (paddr) {
	case 12: *(uint64_t*)(p->cb_param) = in; break;
	default: return trap(v, TRAP_BUS_ERROR);
	}
	return 0;
}

static int io_uart_open(vm_io_t *p, vm_t *v, void *resource) {
	assert(p && v);
	UNUSED(resource);
	return 0;
}

static int io_uart_close(vm_io_t *p, vm_t *v) {
	assert(p && v);
	return 0;
}

static int io_uart_load(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t *out) {
	assert(p && v);
	assert(out);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	paddr -= p->paddr_lo;
	switch (paddr) {
	case 12: *out = wrap_getch(); break;
	default: return trap(v, TRAP_BUS_ERROR);
	}
	return 0;
}

static int io_uart_store(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t in) {
	assert(p && v);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	switch (paddr) {
	case 12: putch(in); break;
	default: return trap(v, TRAP_BUS_ERROR);
	}
	return 0;
}

typedef struct {
	uint64_t m[DISK_SIZE];
	uint64_t page[PAGE/8ull];
	uint64_t block, mode;
	FILE *file;
} disk_t;

static int io_disk_open(vm_io_t *p, vm_t *v, void *resource) {
	assert(p && v);
	if (!resource)
		return 0;
	disk_t *dsk = calloc(1, sizeof (disk_t));
	if (!dsk)
		return -1;
	dsk->file = fopen(resource, "r+b");
	if (!(dsk->file)) {
		free(dsk);
		return -1;
	}
	p->cb_param = dsk;
	return 0;
}

static int io_disk_close(vm_io_t *p, vm_t *v) {
	assert(p && v);
	disk_t *dsk = p->cb_param;
	int r = 0;
	if (dsk && dsk->file)
		r = fclose(dsk->file);
	free(dsk);
	p->cb_param = NULL;
	return r != 0 ? -1 : 0;
}

static int io_disk_load(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t *out) {
	assert(p && v);
	assert(out);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	disk_t *dsk = p->cb_param;
	paddr -= p->paddr_lo;
	switch (paddr) {
	case 12: *out = dsk->block;  return 0;
	case 16: *out = dsk->mode;   return 0;
	default:
		if (within(paddr, p->paddr_lo + PAGE, p->paddr_hi)) {
			*out = dsk->page[paddr/8ull];
			return 0;
		}
	}
	return 0;
}

static int io_disk_store(vm_io_t *p, vm_t *v, uint64_t paddr, uint64_t in) {
	assert(p && v);
	assert(within(paddr, p->paddr_lo + 12ull, p->paddr_hi));
	disk_t *dsk = p->cb_param;
	paddr -= p->paddr_lo;
	switch (paddr) {
	case 12: dsk->block = in; return 0;
	case 16: 
		bit_copy(&dsk->mode, in, 0);
		if (bit_is_set(in, 1)) {
			return trap(v, TRAP_DISABLED);/* TODO: File operation - seek + read ^ write */
		}
		return 0;
	default:
		if (within(paddr, p->paddr_lo + PAGE, p->paddr_hi)) {
			dsk->page[paddr/8ull] = in;
			return 0;
		}
	}
	return trap(v, TRAP_BUS_ERROR);
}

static const vm_t vm_default = {
	.io = { /* TODO: Patch up addresses for .paddr_hi && .paddr_lo, in init(), and setup defaults */
		{ .id = IO_INFO,       .size = PAGE, .load = io_info_load, },
		{ .id = IO_INTERRUPTs, .size = PAGE, },
		{ .id = IO_MMU,        .size = PAGE, },
		{ .id = IO_TIMER,      .size = PAGE, .open = io_timer_open, .update = io_timer_update,.store = io_timer_store, .load = io_timer_load },
		{ .id = IO_UART,       .size = PAGE, .open = io_uart_open, .close = io_uart_close, .store = io_uart_store, .load = io_uart_load },
		{ .id = IO_DISK,       .size = 2ull *PAGE, .open = io_disk_open, .close = io_disk_close, .store = io_disk_store, .load = io_disk_load },
		//{ .id = IO_NETWORK,    .size = PAGE, },
		//{ .id = IO_MOUSE,      .size = PAGE, },
		//{ .id = IO_KEYBOARD,   .size = PAGE, },
		//{ .id = IO_VIDEO,      .size = PAGE, },
		//{ .id = IO_AUDIO,      .size = PAGE, },
	},
	.Z    = 1,
	.PRIV = 1,
	.REAL = 1,
	.PC   = RAM_ADDR / 8ull,
	.IE   = 0,
};

static int io_update(vm_t *v) {
	assert(v);
	for (size_t i = 0; i < PERIPHERAL_SLOTS; i++) {
		vm_io_t *p = &v->io[i];
		p->update(p, v);
	}
	return 0;
}

static void sreg(vm_t *v, unsigned reg, uint64_t value) {
	assert(v);
	v->r[reg] = value;
	v->Z = value == 0;
	v->N = value & 0x8000000000000000ull;
}

#define TLB_ADDR_MASK (0x0000FFFFFFFFFFFFFFFFull & ~PAGE_MASK)
#define TLB_FLAG_MASK (0xFFFF0000000000000000ull)
#define TLB_PADR_MASK (PAGE - 1ull)

static int tlb_lookup(vm_t *v, uint64_t va, uint64_t *pa, int rwx) { /* 0 == found, 1 == thrown trap */
	assert(pa);
	assert(rwx == READ || rwx == WRITE || rwx == EXECUTE);
	*pa = 0;
	/* TODO: assert PAGE is power of 2 */

	const uint64_t low = va & TLB_PADR_MASK;
	va &= TLB_ADDR_MASK;

	for (size_t i = 0; i < TLB_SIZE; i++) {
		vm_tlb_t *t = &v->tlb[i];
		if (bit_is_set(t->paddr, TLB_ENTRY_FLAGS_INUSE) && t->vaddr == va) {
			if (v->PRIV == 0)
				if (0 == bit_is_set(t->paddr, TLB_ENTRY_FLAGS_PRIVL))
					return trap(v, TRAP_PROTECTION);
			int bit = 0;
			switch (rwx) {
			case READ:    bit = TLB_ENTRY_FLAGS_READ;    break;
			case WRITE:   bit = TLB_ENTRY_FLAGS_WRITE;   break;
			case EXECUTE: bit = TLB_ENTRY_FLAGS_EXECUTE; break;
			}

			if (0 == bit_is_set(t->paddr, bit))
				return trap(v, TRAP_PROTECTION);

			bit_set(&t->paddr, TLB_ENTRY_FLAGS_ACCESSED);
			if (rwx == WRITE)
				bit_set(&t->paddr, TLB_ENTRY_FLAGS_DIRTY);
			*pa = (TLB_ADDR_MASK & t->paddr) + low;
			return 0;
		}
	}
	/* TODO: Look in memory at the page tables (PTE - page table entry), 
	 * the page tables will have privilege set, but the TLB can 
	 * look at them. *OR* do it the MIPs way and thrown an exception. */
	return trap(v, TRAP_UNMAPPED);
}

static int tlb_flush_single(vm_t *v, uint64_t va) {
	assert(v);
	assert(v->PRIV);
	va &= TLB_ADDR_MASK;
	for (size_t i = 0; i < TLB_SIZE; i++) {
		vm_tlb_t *t = &v->tlb[i];
		if (bit_is_set(t->paddr, TLB_ENTRY_FLAGS_INUSE) && t->vaddr == va) {
			bit_clear(&t->paddr, TLB_ENTRY_FLAGS_INUSE);
			return 1;
		}
	}
	return 0;
}

static int loadio(vm_t *v, uint64_t address, uint64_t *value) {
	assert(v);
	assert(value);
	assert(within(address, IO_ADDR, RAM_ADDR - 8ull));
	for (size_t i = 0; i < PERIPHERAL_SLOTS; i++) {
		vm_io_t *p = &v->io[i];
		if (within(address, p->paddr_lo, p->paddr_hi))
			return p->load(p, v, address, value);
	}
	return trap(v, TRAP_BUS_ERROR);
}

static int storeio(vm_t *v, uint64_t address, uint64_t value) {
	assert(v);
	assert(within(address, IO_ADDR, RAM_ADDR - 8ull));
	for (size_t i = 0; i < PERIPHERAL_SLOTS; i++) {
		vm_io_t *p = &v->io[i];
		if (within(address, p->paddr_lo, p->paddr_hi))
			return p->store(p, v, address, value);
	}
	return trap(v, TRAP_BUS_ERROR);
}

static int loader(vm_t *v, uint64_t address, uint64_t *value, int rwx) {
	assert(v);
	assert(value);
	*value = 0;
	if (address & 7ull)
		return trap(v, TRAP_UNALIGNED);
	/* TODO: Store registers for paddr and vaddr, on trap */
	if (v->REAL == 0) {
		uint64_t paddr = 0;
		const int st = tlb_lookup(v, address, &paddr, rwx);
		if (st != 0)
			return -1;
		address = paddr;
	}

	if (address >= RAM_ADDR) {
		if (address >= RAM_ADDR + RAM_SIZE)
			return trap(v, TRAP_BUS_ERROR);
		*value = v->m[(address - RAM_ADDR)/8ull];
		return 0;
	} else if (address > IO_ADDR) {
		return loadio(v, address, value);
	} 
	return trap(v, TRAP_BUS_ERROR);
}

static int loadw(vm_t *v, uint64_t address, uint64_t *value) {
	return loader(v, address, value, READ);
}

static int loadwx(vm_t *v, uint64_t address, uint64_t *value) {
	return loader(v, address, value, EXECUTE);
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
	/* TODO: Store registers for paddr and vaddr, with value, on trap */
	if (address & 7ull)
		return trap(v, TRAP_UNALIGNED);

	if (v->REAL == 0) {
		uint64_t paddr = 0;
		const int st = tlb_lookup(v, address, &paddr, WRITE);
		if (st != 0)
			return -1;
		address = paddr;
	}

	if (address >= RAM_ADDR) {
		if (address > RAM_ADDR + RAM_SIZE)
			return trap(v, TRAP_BUS_ERROR);
		v->m[(address - RAM_ADDR)/8ull] = value;
		return 0;
	} else if (address > IO_ADDR) {
		return storeio(v, address, value);
	}
	return trap(v, TRAP_BUS_ERROR);
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
	const uint64_t leading = ((uint64_t)(-1ll)) << ((sizeof(v)*CHAR_BIT) - p - 1);
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

/* TODO: 64-bit floating point arithmetic */
static uint32_t fp_add(vm_t *v, uint32_t x, uint32_t y, int ubit, int vbit) {
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
		x3 = shift > 31 ? (int32_t)arshift32(x0, 31) : x0 >> shift;
		y3 = y0;
	} else {
		const uint32_t shift = xe - ye;
		e0 = xe;
		x3 = x0;
		y3 = shift > 31 ? (int32_t)arshift32(y0, 31) : y0 >> shift;
	}

	uint32_t sum = ((xs << 26) | (xs << 25) | (x3 & 0x01FFFFFFul))
	+ ((ys << 26) | (ys << 25) | (y3 & 0x01FFFFFFul));

	uint32_t s = (((sum & (1 << 26)) ? -sum : sum) + 1) & 0x07FFFFFFul;

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

static uint32_t fp_mul(vm_t *v, uint32_t x, uint32_t y) {
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
	else if ((e1 & 0x100ul) == 0)
		return sign | ((e1 & 0xFFul) << 23) | (z0 >> 1);
	else if ((e1 & 0x80ul) == 0)
		return sign | (0xFFul << 23) | (z0 >> 1);
	return 0;
}

static uint32_t fp_div(vm_t *v, uint32_t x, uint32_t y) {
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
	if ((q1 & (1ul << 25)) != 0) {
		e1++;
		q2 = (q1 >> 1) & 0xFFFFFFul;
	} else {
		q2 = q1 & 0xFFFFFFul;
	}
	uint32_t q3 = q2 + 1ul;

	if (xe == 0)
		return 0;
	else if (ye == 0)
		return sign | (0xFFul << 23);
	else if ((e1 & 0x100ul) == 0)
		return sign | ((e1 & 0xFFul) << 23) | (q3 >> 1);
	else if ((e1 & 0x80ul) == 0)
		return sign | (0xFFul << 23) | (q2 >> 1);
	return 0;
}

static idiv_t divide(uint64_t x, uint64_t y, int is_signed_div) {
	const int sign = ((int64_t)x < 0) && is_signed_div;
	uint64_t x0 = sign ? -x : x;

	typedef struct { uint64_t hi, lo; } u128_t;
	u128_t RQ = { .lo = x0 };
	for (int S = 0; S < 64; S++) {
		const uint64_t w0 = (RQ.lo >> 63) | (RQ.hi << 1);
		const uint64_t w1 = w0 - y;
		if ((int64_t)w1 < 0)
			RQ = (u128_t) { .hi = w0, .lo = (RQ.lo & 0x7FFFFFFFFFFFFFFFull) << 1 };
		else
			RQ = (u128_t) { .hi = w1, .lo = ((RQ.lo & 0x7FFFFFFFFFFFFFFFull) << 1) | 1ull };
	}

	idiv_t d = { RQ.lo, RQ.hi };
	if (sign) {
		d.quot = -d.quot;
		if (d.rem) {
			d.quot -= 1;
			d.rem = y - d.rem;
		}
	}
	return d;
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

	if (print_value(d->out, v->PC * 8ull) < 0)
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
			(v->IPC & ((1ull << 48) - 1ull));
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
	int st = loadwx(v, v->PC * 8ull, &ir);

	if (debug(v, d, ir) < 0)
		return -1;

	if (st == 0)
		v->PC++;
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
				/* TODO: Remove when new special register is implemented */
				a_val = ((uint64_t)v->N    << 63) |
					((uint64_t)v->Z    << 62) |
					((uint64_t)v->C    << 61) |
					((uint64_t)v->V    << 60) |
					((uint64_t)v->IE   << 59) |
					((uint64_t)v->PRIV << 58) |
					((uint64_t)v->REAL << 57) |
					(v->IPC & ((1ull << 48) - 1ull));
			} else {
				a_val = v->H;
			}
			break;
		case TRP: a_val = b_val; trap(v, b_val); break;
		case TLB: if ((ir & ubit) == 0) { /* TODO: more memory instructions, merge with trap? */
				  if (!tlb_flush_single(v, b_val)) {
				  }
			  } else {
			  }
			  break;
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
				multiply(b_val, c_val, &v->H, &a_val);
			}
			break;
		case DIV: 
			if (c_val == 0) {
				(void)trap(v, TRAP_DIV0);
			} else if ((int64_t)c_val > 0) {
				if ((ir & ubit) == 0) {
					a_val = (int64_t)b_val / (int64_t)c_val;
					v->H = (int64_t)b_val % (int64_t)c_val;
					if ((int64_t)v->H < 0) {
						a_val--;
						v->H += c_val;
					}
				} else {
					a_val = b_val / c_val;
					v->H = b_val % c_val;
				}
			} else {
				const idiv_t q = divide(b_val, c_val, ir & ubit);
				a_val = q.quot;
				v->H = q.rem;
			}
			break;
		/* 32-bit floats are shifted so that the negative flag is set */
		case FAD: a_val = (uint64_t)fp_add(v, b_val >> 32, c_val, !!(ir & ubit), !!(ir & vbit)) << 32; break;
		case FSB: a_val = (uint64_t)fp_add(v, b_val >> 32, (c_val ^ 0x8000000000000000ull) >> 32, !!(ir & ubit), !!(ir & vbit)) << 32; break;
		case FML: a_val = (uint64_t)fp_mul(v, b_val >> 32, c_val) << 32; break;
		case FDV: a_val = (uint64_t)fp_div(v, b_val >> 32, c_val) << 32; break;
		}
		sreg(v, a, a_val);
	} else if ((ir & qbit) == 0) { /* memory instructions */
		const uint64_t a = (ir & 0x0F00000000000000ull) >> 56;
		const uint64_t b = (ir & 0x00F0000000000000ull) >> 52;
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
				sreg(v, 15, v->PC * 8ull);

			if ((ir & ubit) == 0) {
				uint64_t c = ir & 0x0000000F00000000ull;
				v->PC = v->r[c] / 8ull;
			} else {
				uint64_t off = ir & 0x00FFFFFFFFFFFFFFull;
				off = (off ^ 0x0080000000000000ull) - 0x0080000000000000ull;
				v->PC = v->PC + off;
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

static int deinit(vm_t *v) {
	assert(v);
	int r = 0;
	for (size_t i = 0; i < PERIPHERAL_SLOTS; i++) {
		vm_io_t *p = &v->io[i];
		if (p->close(p, v) < 0)
			r = -1;
	}
	return r;
}

static int init(vm_t *v) { /* TODO: Initialize disk */
	assert(v);
	*v = vm_default;
	uint64_t addr = IO_ADDR;
	assert(((addr & (PAGE - 1ull)) == 0));
	for (size_t i = 0; i < PERIPHERAL_SLOTS; i++) {
		vm_io_t *p = &v->io[i];
		p->open   = p->open   ? p->open   : io_default_open;
		p->close  = p->close  ? p->close  : io_default_close;
		p->update = p->update ? p->update : io_default_update;
		p->load   = p->load   ? p->load   : io_default_load;
		p->store  = p->store  ? p->store  : io_default_store;
		p->paddr_lo = addr;
		p->paddr_hi = addr + p->size - 8ull;
		addr += p->size;
		p->car = p->paddr_lo;
		p->cdr = addr;
		assert(((addr & (PAGE - 1ull)) == 0));
		if (p->open(p, v, NULL) < 0)
			goto fail;
	}
	return 0;
fail:
	(void)deinit(v);
	return -1;
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

int tests(void) {
	return 0;
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
License : ISC (2014 Peter De Wachter, 2020 Richard James Howe)\n\n\
Options :\n\n\
-h\tprint this help text and exit successfully\n\
-t\trun the internal tests and return test result (1 = failure, 0 = success)\n\
-x\tfiles are stored as text in a hex format\n\
-D\tturn debugging on\n\
-k file\tset file for kernel\n\
-d file\tset file for disk image\n\
-c num\tset number of instructions to run for (0 = run forever, default)\n\
\n\
This program returns zero on success and negative on failure.\n\
\n\
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
		goto fail;
	if (setvbuf(stdout, obuf, _IOFBF, sizeof obuf) < 0)
		goto fail;

	/* TODO: Remove 'k' and load kernel off disk via 'BIOS' */
	for (int ch = 0; (ch = vm_getopt(&opt, argc, argv, "htxDk:d:c:")) != -1; ) {
		switch (ch) {
		case 'h': help(stdout, argv[0]); return 0;
		case 'x': is_hex ^= 1; break;
		case 'D': d.on = 1; break;
		case 'c': cycles = atol(opt.arg); break;
		case 't': { 
			const int fail = tests(); 
			fprintf(stdout, "tests passed: %s\n", fail ? "no" : "yes"); 
			return fail; 
		}
		case 'k':
			if (load(opt.arg, &v.m[0], RAM_SIZE, is_hex) < 0) {
				fprintf(stderr, "load kernel failed: %s\n", argv[1]);
				goto fail;
			}
			break;
		case 'd':
			if (load(opt.arg, &v.p.disk[0], DISK_SIZE, is_hex) < 0) {
				fprintf(stderr, "load disk failed: %s\n", argv[2]);
				goto fail;
			}
			break;
		default: help(stderr, argv[0]); return 1;
		}
	}

	if (run(&v, &d, cycles) < 0) {
		fprintf(stderr, "run failed\n");
		goto fail;
	}
	return deinit(&v);
fail:
	(void)deinit(&v);
	return 1;
}

