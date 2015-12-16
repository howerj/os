/*Originally from: <http://wiki.osdev.org/Bare_Bones>*/
#include "klib.h"
#include "timer.h"
#include "monitor.h"
 
/* This tutorial will only work for the 32-bit ix86 targets. Cross compiled */
#if !defined(__i386__) || defined(__linux__)
#error "This kernel needs to be cross compiled compiled with a ix86-elf compiler"
#endif
 
void kernel_main(void)
{
        monitor_clear();
        monitor_set_background_color(COLOR_RED);
        monitor_set_foreground_color(COLOR_BLUE);
        monitor_printf("Kernel v0.01a Start\n", 0, 1);
        monitor_default_colors();
        init_timer(50);
}

