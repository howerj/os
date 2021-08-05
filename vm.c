/* Richard James Howe, howe.r.j.89@gmail.com, Virtual Machine, Public Domain */
/* TODO: Networking (debug)
 * TODO: Screen/Keyboard/Mouse/Sound
 * TODO: Floating point
 * TODO: Misc: Forth/BIOS ROM, Device/Peripheral discovery/description table, debugging */
#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
	uint8_t buf[8192];
	size_t len;
	void *handle;
	int error;
} network_t;

#ifdef USE_NETWORKING
#include <pcap.h>
#define NETWORKING (1ull)
static int eth_init(network_t *n) {
	char errbuf[PCAP_ERRBUF_SIZE] = { 0, };
	pcap_if_t *devices;
	if (pcap_findalldevs(&devices, errbuf) == -1) {
		(void)fprintf(stderr, "Error eth_init: %s\n", errbuf);
		goto fail;
	}
	pcap_if_t *device;
	for (device = devices; device; device = device->next) {
		if (device->description) {
			printf(" (%s)\n", device->description);
		} else {
			(void)fprintf(stderr, "No device\n");
			//goto fail;
		}
	}
	device = devices->next->next;
	if (NULL == (n->handle = pcap_open_live(device->name , 65536, 1, 10, errbuf))) {
		fprintf(stderr, "Unable to open the adapter\n");
		pcap_freealldevs(devices);
		goto fail;
	}
	pcap_freealldevs(devices);
	return 0;
fail:
	n->error = -1;
	return -1;
}

static int eth_poll(network_t *n) {
	assert(n);
	const u_char *packet = NULL;
	struct pcap_pkthdr *header = NULL;
	for(int res = 0; res == 0;)
		res = pcap_next_ex(n->handle, &header, &packet);
	n->len = header->len;
	memcpy(n->buf, packet, n->len);
	return n->len;
}

static int eth_transmit(network_t *n) {
	return pcap_sendpacket(n->handle, (u_char *)(n->buf), n->len);
}
#else
#define NETWORKING (0ull)
static int eth_init(network_t *n) { assert(n); n->error = -1; return -1; }
static int eth_poll(network_t *n) { assert(n); return -1; }
static int eth_transmit(network_t *n) { assert(n); return -1; }
#endif

#ifdef USE_GUI
#include <SDL.h>
#define GUI (1ull)
#else
#define GUI (0ull)
#endif

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
static int getch(void) { return getchar(); }
static int putch(const int c) { return putchar(c); }
static void sleep_ms(unsigned ms) { (void)ms; }
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

/* End of Peripherals - start of VM */

static inline int within(uint64_t addr, uint64_t lo, uint64_t hi) { return addr >= lo && addr < hi; }

static inline int bit_get(uint64_t v, int bit) { return !!(v & (1ull << bit)); }
static inline int bit_set(uint64_t *v, int bit) { assert(v); *v = *v |  (1ull << bit); return 1; }
static inline int bit_xor(uint64_t *v, int bit) { assert(v); *v = *v ^  (1ull << bit); return bit_get(*v, bit); }
static inline int bit_clr(uint64_t *v, int bit) { assert(v); *v = *v & ~(1ull << bit); return 0; }
static inline int bit_cnd(uint64_t *v, int bit, int set) { assert(v); return (set ? bit_set : bit_clr)(v, bit); }

#define MEMORY_START (0x0000000080000000ull)
#define MEMORY_END   (MEMORY_START + (sizeof (((vm_t) { .pc = 0 }).m)/ sizeof (uint64_t)))
#define SIZE         (1024ul * 1024ul * 1ul)
#define TLB_ENTRIES  (64ul)
#define TRAPS        (32ul)
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#define PAGE_SIZE    (8192ull)
#define PAGE_MASK    (PAGE_SIZE - 1ull)
#define IO_START     (0x0000000004000000ull)
#define IO_END       (0x0000000008000000ull)
#define TLB_ADDR_MASK (0x0000FFFFFFFFFFFFull & ~PAGE_MASK)
#define NELEMS(X)    (sizeof (X) / sizeof ((X)[0]))
#define REGS         (16ul)

typedef struct {
	uint64_t m[SIZE / sizeof (uint64_t)];
	uint64_t pc, flags, timer, tick, tron;
	uint64_t r[REGS];
	uint64_t traps[TRAPS];
	uint64_t tlb_va[TLB_ENTRIES], tlb_pa[TLB_ENTRIES];
	uint64_t disk[SIZE / sizeof (uint64_t)], dbuf[PAGE_SIZE], dstat, dp;
	uint64_t uart_control, uart_rx, uart_tx;
	uint64_t rtc_control, rtc_s, rtc_frac_s;
	uint64_t loaded;
	network_t network;
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
	if (trace(v, "+trap,%"PRIx64",%"PRIx64",%"PRIx64",", v->flags, addr, val) < 0)
		return -1;
	uint8_t level = v->flags;
	const uint64_t a = v->flags >> 8;
	const uint64_t b = v->flags >> 16;
	if (level > 2) { v->halt = -1; return -1; }
	level++;
	v->flags &= 0xF0F0FFFFFFFFFF00ull; /* clear saved flags */
	v->flags |= level;
	const uint64_t f = v->flags & (0xF0F0ull << 48);
	v->flags |= (f >> 4);
	v->r[a % REGS] = v->pc;
	v->r[b % REGS] = val;
	bit_set(&v->flags, PRIV);   /* escalate privilege level */
	if (addr >= NELEMS(v->traps))
		addr = T_ADDR;
	v->pc = v->traps[addr];
	return 1;
}

/* NB. It might be better to start off with a large page size, so everything
 * can be stored within the TLB without fault. */
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
		/* PAGE 3 = Disk Control */
		case IO(3, 0): *val = v->dstat; return 0;
		case IO(3, 1): 
			bit_clr(&v->dstat, 0); /* never busy */
			bit_clr(&v->dstat, 1); /* do operation always reads 0 */
			*val = v->dstat & 0x1Full;
			return 0;
		/* PAGE 4 = Disk Buffer */
		}

		if (within(addr, IO(4, 0), IO(4, 0) + (sizeof (v->dbuf)/sizeof (uint64_t)))) {
			*val = v->dbuf[addr - IO(4, 0)];
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
		/* PAGE 3 = Disk Control */
		case IO(3, 0): 
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
		case IO(3, 1): v->dp = val; return 0;
		/* PAGE 4 = Disk Buffer */
		}

		if (within(addr, IO(4, 0), IO(4, 0) + (sizeof (v->dbuf)/sizeof (uint64_t)))) {
			v->dbuf[addr - IO(4, 0)] = val;
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

static int cpu(vm_t *v) {
	assert(v);
	uint64_t instr = 0, npc = v->pc + sizeof(uint64_t);
	if (loadw(v, v->pc, &instr, EXECUTE))
		goto trapped;
	const uint32_t op = instr >> 32;
	const uint32_t op1 = instr & 0xFFFFFFFFul;
	const uint8_t ras = op;
	const uint8_t rbs = op >> 8;
	const uint8_t alu = op >> 16;
	const uint8_t b = rbs & 7;
	const uint8_t a = ras & 7;
	uint64_t rb = v->r[b];
	uint64_t ra = v->r[a];
	uint64_t trap_addr = 0;
	uint64_t trap_val = v->pc;

	if (trace(v, "+pc,%"PRIx64",%"PRIx64",%"PRIx64",", v->pc, instr, op1) < 0)
		return -1;
	if ((op & 0x80000000ul) && !bit_get(v->flags, V))
		goto next;
	if ((op & 0x40000000ul) && !bit_get(v->flags, C))
		goto next;
	if ((op & 0x20000000ul) && !bit_get(v->flags, Z))
		goto next;
	if ((op & 0x10000000ul) && !bit_get(v->flags, N))
		goto next;
	uint64_t nra = ra;

	if (ras & 0x1) { ra = op1; }
	if (ras & 0x2) { ra = ra & 0x0000000080000000ull ? ra | 0xFFFFFFFF00000000ull : ra; }
	if (ras & 0x4) { ra += v->pc; }
	if (rbs & 0x1) { rb = op1; }
	if (rbs & 0x2) { rb = rb & 0x0000000080000000ull ? rb | 0xFFFFFFFF00000000ull : rb; }
	if (rbs & 0x4) { rb += v->pc; }
	if (ras & 0x8 || rbs & 0x8) { trap_addr = T_INST; goto on_trap; }

	switch (alu) {
	case  0: nra = ra; break;
	case  1: nra = rb; break;
	case  2: nra = ~ra; break;
	case  3: nra = ra & rb; break;
	case  4: nra = ra | rb; break;
	case  5: nra = ra ^ rb; break;
	case  6: nra = ra << rb; break;
	case  7: nra = ra >> rb; break;
	case  8: nra = ra * rb; break;
	case  9: if (!rb) { trap_addr = T_DIV0; goto on_trap; } nra = ra / rb; break;
	case 10: ra += bit_get(v->flags, C); /* fall-through */
	case 11: nra = ra + rb; bit_cnd(&v->flags, C, nra < ra); bit_cnd(&v->flags, V, ((nra ^ ra) & (nra ^ rb)) >> 63); break;
	case 12: ra -= bit_get(v->flags, C); /* fall-through */
	case 13: nra = ra - rb; bit_cnd(&v->flags, C, nra > ra); bit_cnd(&v->flags, V, ((nra ^ ra) & (nra ^ rb)) >> 63); break;
	/* NB. Need to add floating point instructions, which will also set arithmetic flags */

	case 32: npc = ra; break; /* jump */
	case 33: nra = npc; npc = ra; break; /* link */

	case 48: nra = v->flags; break;
	case 49:
		if (bit_get(v->flags, PRIV) == 0) {
			v->flags &= ~(0xFull << 52);
			v->flags |= (ra & (0xFull << 52));
			break;
		}
		v->flags = ra;
		break;
	case 50: nra = v->traps[ra % TRAPS]; break;
	case 51:
		if (bit_get(v->flags, PRIV) == 0) {
			trap_addr = T_PRIV;
			goto on_trap;
		}
		v->traps[ra % TRAPS] = rb;
		break;
	case 64: if (loadw(v, ra, &nra, READ)) goto trapped; break;
	case 65: if (storew(v, ra, rb)) goto trapped; break;
	case 66: { uint8_t byte = 0; if (loadb(v, ra, &byte)) goto trapped; nra = byte; } break;
	case 67: if (storeb(v, ra, rb)) goto trapped; break;

	case 80: trap_addr = ra; trap_val = rb; goto on_trap;
	case 81: if (tlb_flush_single(v, ra, &nra)) goto on_trap; break;
	case 82: if (tlb_flush_all(v)) goto trapped; break;
	case 83: {
		if (bit_get(v->flags, PRIV) == 0) { 
			trap_addr = T_PRIV; 
			goto on_trap; 
		}
		const int va = bit_get(ra, 15);
		bit_clr(&nra, 15);
		if (nra > NELEMS(v->tlb_va)) { 
			trap_addr = T_INST; 
			goto on_trap; 
		}
		if (va) v->tlb_va[ra] = rb; else v->tlb_pa[ra] = rb;
		}
		break;
	default:
		trap_addr = T_INST;
	}

	v->r[a] = nra;
	if (nra == 0)
		bit_set(&v->flags, Z);
	if ((nra & (1ull << 63)))
		bit_set(&v->flags, N);
next:
	v->pc = npc;
	return 0;
on_trap:
	return trap(v, trap_addr, trap_val);
trapped:
	return 1;
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
	static vm_t v;
	v.pc = MEMORY_START;
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
