#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

/* Hardware text mode color constants. */
typedef enum 
{
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
} monitor_color;

void monitor_putc(char c);
void monitor_clear(void);
void monitor_puts(char *s);
void monitor_set_background_color(monitor_color color);
void monitor_set_foreground_color(monitor_color color);
void monitor_default_colors(void);

#endif
