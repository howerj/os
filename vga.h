#ifndef VGA_H
#define VGA_H

/* Hardware text mode color constants. */
typedef enum {
	COLOR_BLACK = 0u,
	COLOR_BLUE = 1u,
	COLOR_GREEN = 2u,
	COLOR_CYAN = 3u,
	COLOR_RED = 4u,
	COLOR_MAGENTA = 5u,
	COLOR_BROWN = 6u,
	COLOR_LIGHT_GREY = 7u,
	COLOR_DARK_GREY = 8u,
	COLOR_LIGHT_BLUE = 9u,
	COLOR_LIGHT_GREEN = 10u,
	COLOR_LIGHT_CYAN = 11u,
	COLOR_LIGHT_RED = 12u,
	COLOR_LIGHT_MAGENTA = 13u,
	COLOR_LIGHT_BROWN = 14u,
	COLOR_WHITE = 15u
} vga_color;

int vga_putc(char c);
int vga_clear(void);
int vga_set_background_color(vga_color color);
int vga_set_foreground_color(vga_color color);
int vga_default_colors(void);

#endif
