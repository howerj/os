/* Richard James Howe, howe.r.j.89@gmail.com, Virtual Machine, Public Domain */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define SIZE (1024ul * 1024ul * 8ul)
#define ESCAPE (27)
#define DELETE (127)
#define BACKSPACE (8)

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
	if (ch == ESCAPE)
		exit(0);
	return ch == DELETE ? BACKSPACE : ch;
}


typedef struct {
	uint64_t m[SIZE / sizeof(uint64_t)];
	uint64_t r[32];
	uint64_t pc, count;
} vm_t;

/* TODO: CPU/MMU/Trap/Interrupts/Timer/Hard-drive
 *       CPU must have relative addressing as optional
 * TODO: UART/Networking
 * TODO: Screen/Keyboard/Mouse/Sound
 * TODO: Floating point */

int run(vm_t *v) {
	return 0;
}

int main(int argc, char **argv) {
	unsigned long loaded = 0;
	static vm_t v = { .pc = SIZE / 2ul, };
	if (setup() < 0)
		return -1;
	if (argc != 2)
		return 1;
	FILE *fin = fopen(argv[1], "rb");
	if (!fin)
		return 2;
	for (loaded = 0; loaded < SIZE; loaded++) {
		unsigned long long v = 0;
	}
	if (fclose(fin) < 0)
		return 3;
	if (run(&v) < 0)
		return 4;
	FILE *fout = fopen(argv[1], "wb");
	if (!fout)
		return 5;
	if (fclose(fout) < 0)
		return 6;
	return 0;
}
