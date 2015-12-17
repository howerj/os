/*Originally from: <http://wiki.osdev.org/Bare_Bones>*/
#include "klib.h"
#include "timer.h"
#include "gdt.h"
#include "monitor.h"
 
/* This tutorial will only work for the 32-bit ix86 targets. Cross compiled */
#if !defined(__i386__) || defined(__linux__)
#error "This kernel needs to be cross compiled compiled with a ix86-elf compiler"
#endif
 
void kernel_main(uint32_t p)
{
        monitor_clear();
        monitor_printf("@todo Push correct value: %d %d\n", p, 99);
        init_descriptor_tables();
        monitor_set_background_color(COLOR_RED);
        monitor_set_foreground_color(COLOR_BLUE);
        monitor_printf("Kernel v0.01a Start\n");
        monitor_default_colors();
        asm volatile("int $0x3");
        asm volatile("int $0x4");
        asm volatile("sti");
        init_timer(50);
}

