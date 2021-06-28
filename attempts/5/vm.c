/* Author: Richard James Howe
 * License: The Unlicense
 * Email: howe.r.j.89@gmail.com
 * Project: 64-bit Emulator
 *
 * This is an emulator for a system that could in principle be implemented in
 * hardware. It is meant to be simple, easy to emulate, and viable.
 *
 * State-0: CPU
 * Stage-1: MMU/TBL, UART, Disk, Timer, Interrupts/Traps
 * State-2: Networking/Floats/SPI Flash (Replaces Disk)
 * Stage-3: Keyboard/Mouse/Graphics/Sound
 * State-4: Specification, Porting system */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MEMSZ (1024ul * 1024ul * 8ul)
#define PAGE  (4096ul)
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) < (Y) ? (Y) : (X))

typedef struct {
	uint64_t m[MEMSZ];
	uint64_t disk[MEMSZ];
	uint64_t cycles, pc;
	uint64_t tick, timer, uart, tron, trap;
	uint64_t disk[1024 * 1024], dbuf[1024], dstat, dp;
	uint64_t traps[TRAPS];
	uint64_t tlb_va[TLB_ENTRIES], tlb_pa[TLB_ENTRIES];
} vm_t;

static int vm(vm_t *v) {
	assert(v);
	for (;;) {
		v->cycles++;
	}
	return 0;
}

static int disk_save(vm_t *v, const char *file) {
	assert(v);
	assert(file);
	FILE *f = fopen(file, "wb");
	if (!f)
		return -1;
	const int r = fwrite(v->disk, 1, sizeof (v->disk), f);
	return fclose(f) < 0 ? -1 : r; 
}

static int disk_load(vm_t *v, const char *file) {
	assert(v);
	assert(file);
	FILE *f = fopen(file, "rb");
	if (!f)
		return -1;
	const size_t sz = fread(v->disk, 1, sizeof (v->disk), f);
	memcpy(v->m, v->disk, MIN(PAGE * 8, sz));
	return fclose(f);
}

int main(int argc, char **argv) {
	static vm_t v = { .m = { 0, } };
	if (argc != 2) {
		(void)fprintf(stderr, "usage: %s disk.bin", argv[0]);
		return 1;
	}
	if (disk_load(&v, argv[1]) < 0) /* First PAGE copied as bootloader */
		return 1;
	const int r = vm(&v);
	if (disk_save(&v, argv[1]) < 0)
		return 1;
	return r;
}

