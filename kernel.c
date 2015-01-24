/*Originally from: <http://wiki.osdev.org/Bare_Bones>*/
#include <stdbool.h> 
#include <stddef.h>
#include <stdint.h>
 
/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__) || defined(__linux__)
#error "This kernel needs to be cross compiled compiled with a ix86-elf compiler"
#endif
 
/* Hardware text mode color constants. */
enum vga_color
{
	COLOR_BLACK = 0,
	COLOR_BLUE = 1,
	COLOR_GREEN = 2,
	COLOR_CYAN = 3,
	COLOR_RED = 4,
	COLOR_MAGENTA = 5,
	COLOR_BROWN = 6,
	COLOR_LIGHT_GREY = 7,
	COLOR_DARK_GREY = 8,
	COLOR_LIGHT_BLUE = 9,
	COLOR_LIGHT_GREEN = 10,
	COLOR_LIGHT_CYAN = 11,
	COLOR_LIGHT_RED = 12,
	COLOR_LIGHT_MAGENTA = 13,
	COLOR_LIGHT_BROWN = 14,
	COLOR_WHITE = 15,
};
 
uint8_t make_color(enum vga_color fg, enum vga_color bg)
{
	return fg | (bg << 4u);
}
 
uint16_t make_vgaentry(char c, uint8_t color)
{
	uint16_t c16 = c;
	uint16_t color16 = color;
	return c16 | (color16 << 8u);
}
 
size_t strlen(const char* str)
{
	size_t ret = 0u;
	while ( str[ret] != 0u )
		ret++;
	return ret;
}
 
static const size_t VGA_WIDTH = 80u;
static const size_t VGA_HEIGHT = 25u;
 
size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;
 
void terminal_initialize()
{
	terminal_row    = 0u;
	terminal_column = 0u;
	terminal_color  = make_color(COLOR_LIGHT_GREY, COLOR_BLACK);
	terminal_buffer = (uint16_t*) 0xB8000u;
	for(size_t y = 0u; y < VGA_HEIGHT; y++){
		for(size_t x = 0u; x < VGA_WIDTH; x++){
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = make_vgaentry(' ', terminal_color);
		}
	}
}
 
void terminal_setcolor(uint8_t color)
{
	terminal_color = color;
}
 
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = make_vgaentry(c, color);
}
 
void terminal_putchar(char c)
{
	terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
	if (VGA_WIDTH == ++terminal_column){
		terminal_column = 0u;
		if (VGA_WIDTH ==  ++terminal_row)
			terminal_row = 0u;
	}
}
 
void terminal_writestring(const char* data)
{
	size_t datalen = strlen(data);
	for(size_t i = 0; i < datalen; i++)
		terminal_putchar(data[i]);
}
 
void kernel_main()
{
	terminal_initialize();
	/* Since there is no support for newlines in terminal_putchar yet, \n will
	   produce some VGA specific character instead. This is normal. */
	terminal_writestring("Hello, kernel World!\n");
}
