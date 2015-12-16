#include "klib.h"

size_t kstrlen(const char* str)
{
	size_t ret = 0u;
	while (*str++)
		ret++;
	return ret;
}

int kstrcmp(const char *s1, const char *s2){
        char a,b;
        while((a = *s1++) && (b = *s2++))
                if(a < b)
                        return -1;
                else
                        return 1;
        if(a < b)
                return -1;
        else if(b > a)
                return 1;
        else
                return 0;
}

size_t kstrspn(const char *s, const char *accept){
        const char *acpt = accept;
        char a, c;
        size_t i = 0;
        while((c = *s++)){
                while((a = *acpt++)){
                        if(a == c){
                                i++;
                                break;
                        } else if(!a){
                                return i;
                        }
                }
                acpt = accept;
        }
        return i;
}

char *kstrcpy(char *dst, const char *src){
        while((*dst++ = *src++));
        return dst;
}

uint32_t kstrtou32(char *nptr){
        uint32_t n = 0;
        char c;
        for(;(c = *nptr++);)
                n = (n * 10u) + (c - '0');
        return n;
}

void* kmemset(void *s, uint8_t c, size_t n){
        size_t i = 0;
        uint8_t* p = (uint8_t *) s;
        while(i++ < n)
                p[i] = c;
        return s;
}

void* kmemmove(void *dst, const void *src, size_t n){
	uint8_t* d = (uint8_t*) dst;
	const uint8_t* s = (const uint8_t*) src;
        size_t i;
	if (d < s)
		for (i = 0; i < n; i++)
			d[i] = s[i];
	else
		for (i = n; i != 0; i--)
			d[i-1] = s[i-1];
	return dst;
}

void outb(uint16_t port, uint8_t value)
{
        asm volatile("outb %1, %0" : : "dN" (port), "a" (value));
}

uint8_t inb(uint16_t port)
{
        uint8_t r;
        asm volatile("inb %1, %0" : "=a" (r): "dN" (port));
        return r;
}

uint16_t inw(uint16_t port)
{
        uint16_t r;
        asm volatile("inw %1, %0" : "=a" (r): "dN" (port));
        return r;
}

char *kreverse(char *s, size_t len)
{
        size_t i = 0;
        char c;
        do {
                c = s[i];
                s[i] = s[len - i];
                s[len - i] = c;
        } while(i++ < (len / 2));
        return s;
}

static const char conv[] = "0123456789abcdefghijklmnopqrstuvwxzy";
int ku32tostr(char *str, size_t len, uint32_t value, int base)
{
        uint32_t i = 0;
        char s[32 + 1] = "";
        if(base < 2 || base > 36)
                return -1;
        do {
                s[i++] = conv[value % base];
        } while((value /= base));
        if(i > len)
                return -1;
        kreverse(s, i-1);
        s[i] = '\0';
        kmemmove(str, s, i);
        return 0;
}

int ks32tostr(char *str, size_t len, int32_t value, unsigned base) 
{ 
        int32_t neg = value;
        uint32_t i = 0, x = value;
        char s[32 + 2] = "";
        if(base < 2 || base > 36)
                return -1;
        if(x > INT32_MAX)
                x = -x;
        do {
                s[i++] = conv[x % base];
        } while((x /= base) > 0);
        if(neg < 0)
                s[i++] = '-';
        if(i > len)
                return -1;
        kreverse(s, i-1);
        s[i] = '\0';
        kmemmove(str, s, i);
        return 0;
}

