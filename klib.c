#include "klib.h"
#include "vga.h"

void _panic(const char *msg, const char *file, const char *func, unsigned line)
{
	kprintf("(panic \"%s\" '%s '%s %d)\n", msg, file, func, line);
	disable();
	for (;;)
		halt();
}

void _assert(int test, const char *expr, const char *file, const char *func, unsigned line)
{
	if(!test) {
		kprintf("(assertion-failed \"%s\" '%s '%s %d)\n", expr, file, func, line);
		disable();
		for (;;)
			halt();
	}
}

size_t kstrlen(const char *str)
{
	size_t ret = 0u;
	while (*str++)
		ret++;
	return ret;
}

int kstrcmp(const char *s1, const char *s2)
{
	char a, b;
	while ((a = *s1++) && (b = *s2++))
		if (a < b)
			return -1;
		else
			return 1;
	if (a < b)
		return -1;
	else if (b > a)
		return 1;
	else
		return 0;
}

size_t kstrspn(const char *s, const char *accept)
{
	const char *acpt = accept;
	char a, c;
	size_t i = 0;
	while ((c = *s++)) {
		while ((a = *acpt++)) {
			if (a == c) {
				i++;
				break;
			} else if (!a) {
				return i;
			}
		}
		acpt = accept;
	}
	return i;
}

char *kstrcpy(char *dst, const char *src)
{
	while ((*dst++ = *src++)) ;
	return dst;
}

uint32_t kstrtou32(char *nptr)
{
	uint32_t n = 0;
	char c;
	for (; (c = *nptr++);)
		n = (n * 10u) + (c - '0');
	return n;
}

void *kmemset(void *s, uint8_t c, size_t n)
{
	size_t i = 0;
	uint8_t *p = (uint8_t *) s;
	while (i++ < n)
		p[i] = c;
	return s;
}

void *kmemmove(void *dst, const void *src, size_t n)
{
	uint8_t *d = (uint8_t *) dst;
	const uint8_t *s = (const uint8_t *)src;
	size_t i;
	if (d < s)
		for (i = 0; i < n; i++)
			d[i] = s[i];
	else
		for (i = n; i != 0; i--)
			d[i - 1] = s[i - 1];
	return dst;
}

char *kreverse(char *s, size_t len)
{
	size_t i = 0;
	char c;
	do {
		c = s[i];
		s[i] = s[len - i];
		s[len - i] = c;
	} while (i++ < (len / 2));
	return s;
}

static const char conv[] = "0123456789abcdefghijklmnopqrstuvwxzy";
int ku32tostr(char *str, size_t len, uint32_t value, int base)
{
	uint32_t i = 0;
	char s[32 + 1] = "";
	if (base < 2 || base > 36)
		return -1;
	do {
		s[i++] = conv[value % base];
	} while ((value /= base));
	if (i > len)
		return -1;
	kreverse(s, i - 1);
	s[i] = '\0';
	kmemmove(str, s, i);
	return 0;
}

int ks32tostr(char *str, size_t len, int32_t value, unsigned base)
{
	int32_t neg = value;
	uint32_t i = 0, x = value;
	char s[32 + 2] = "";
	if (base < 2 || base > 36)
		return -1;
	if (x > INT32_MAX)
		x = -x;
	do {
		s[i++] = conv[x % base];
	} while ((x /= base) > 0);
	if (neg < 0)
		s[i++] = '-';
	if (i > len)
		return -1;
	kreverse(s, i - 1);
	s[i] = '\0';
	kmemmove(str, s, i);
	return 0;
}

int kputc(char c)
{
	return vga_putc(c);
}

int kputs(char *s)
{
	size_t i = 0;
	while (s[i])
		if(vga_putc(s[i++]) < 0)
			return -1;
	return i;
}

static int print_u32(uint32_t d, unsigned base)
{
	char v[34] = "";
	if(ku32tostr(v, 34, d, base) < 0)
		return -1;
	return kputs(v);
}

static int print_s32(int32_t d, unsigned base)
{
	char v[34] = "";
	if(ks32tostr(v, 34, d, base) < 0)
		return -1;
	return kputs(v);
}

static int map_color(char color);
int kprintf(char *fmt, ...)
{
	va_list ap;
	uint32_t b;
	int ret = 0, r = 0;
	va_start(ap, fmt);
	while (*fmt) {
		char f;
		if ('%' == (f = *fmt++)) {
			switch (f = *fmt++) {
			case '0':
				goto finish;
			case '%':
				r = kputc(f);
				break;
			case 'c':
			{
				char c = va_arg(ap, int);
				r = kputc(c);
				break;
			}
			case 's':
			{
				char *s = va_arg(ap, char *);
				r = kputs(s);
				break;
			}
			case 'u':
				b = va_arg(ap, uint32_t);
				r = print_u32(b, 10);
				break;
			case 'x':
				b = va_arg(ap, uint32_t);
				r = print_u32(b, 16);
				break;
			case 'd':
			{
				int32_t a;
				a = va_arg(ap, int32_t);
				r = print_s32(a, 10);
				break;
			}
			case 'C':
				r = vga_clear();
				break;
			case 'D':
				r = vga_default_colors();
				break;
			case 'B':
			case 'F':
				{
					int t = f;
					if ((f = *fmt++)) {
						int m = map_color(f);
						if (m > 0)
							r = (t == 'B') ? vga_set_background_color(m) : vga_set_foreground_color(m);
					} else {
						r = -1;
						goto finish;
					}
					break;
				}
			default:	/*error */
				r = -1;
				goto finish;
			}
		} else {
			r = kputc(f);
		}
		if(r >= 0) {
			ret += r;
		} else {
			ret = -1;
			goto finish;
		}
	}
 finish:
	va_end(ap);
	return ret;
}

/* assembly wrappers */

void outb(uint16_t port, uint8_t value)
{
	asm volatile ("outb %1, %0"::"dN" (port), "a"(value));
}

uint8_t inb(uint16_t port)
{
	uint8_t r;
	asm volatile ("inb %1, %0":"=a" (r):"dN"(port));
	return r;
}

uint16_t inw(uint16_t port)
{
	uint16_t r;
	asm volatile ("inw %1, %0":"=a" (r):"dN"(port));
	return r;
}

/*http://wiki.osdev.org/Inline_Assembly/Examples#I.2FO_access*/
void io_wait(void)
{
	/* Port 0x80 is used for 'checkpoints' during POST. */
	/* The Linux kernel seems to think it is free for use :-/ */
	asm volatile ("outb %%al, $0x80"::"a" (0));
	/* %%al instead of %0 makes no difference.  TODO: does the register need to be zeroed? */
}

void enable(void)
{
	asm volatile ("sti");
}

void disable(void)
{
	asm volatile ("cli");
}

void halt(void)
{
	asm volatile ("hlt");
}

uint64_t time(void)
{
	uint64_t ret;
	asm volatile ("rdtsc":"=A" (ret));
	return ret;
}

/* misc functions */

/* map a character representing a color to a vga color */
static int map_color(char color)
{
	int r = -1;
	switch (color) {
	case 'k':
		r = COLOR_BLACK;
		break;
	case 'b':
		r = COLOR_BLUE;
		break;
	case 'g':
		r = COLOR_GREEN;
		break;
	case 'y':
		r = COLOR_CYAN;
		break;
	case 'r':
		r = COLOR_RED;
		break;
	case 'm':
		r = COLOR_MAGENTA;
		break;
	case 'o':
		r = COLOR_BROWN;
		break;
	case 'e':
		r = COLOR_LIGHT_GREY;
		break;
	case 'E':
		r = COLOR_DARK_GREY;
		break;
	case 'B':
		r = COLOR_LIGHT_BLUE;
		break;
	case 'G':
		r = COLOR_LIGHT_GREEN;
		break;
	case 'Y':
		r = COLOR_LIGHT_CYAN;
		break;
	case 'R':
		r = COLOR_LIGHT_RED;
		break;
	case 'M':
		r = COLOR_LIGHT_MAGENTA;
		break;
	case 'O':
		r = COLOR_LIGHT_BROWN;
		break;
	case 'W':
		r = COLOR_WHITE;
		break;
	default:
		break;
	}
	return r;
}

