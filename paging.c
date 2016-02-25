#include "paging.h"
#include "kheap.h"
#include "klib.h"
#include "monitor.h"

page_directory_t *kernel_directory = NULL;
page_directory_t *current_directory = NULL;

uint32_t *frames;
uint32_t nframes;

#define INDEX_FROM_BIT(X)  (X/(8*4))
#define OFFSET_FROM_BIT(X) (X%(8*4))

static void set_frame(uint32_t frame_addr)
{
        uint32_t frame = frame_addr / 0x1000;
        uint32_t idx   = INDEX_FROM_BIT(frame);
        uint32_t off   = OFFSET_FROM_BIT(frame);
        frames[idx]   |= (0x1 << off);
}

static void clear_frame(uint32_t frame_addr)
{
        uint32_t frame = frame_addr / 0x1000;
        uint32_t idx   = INDEX_FROM_BIT(frame);
        uint32_t off   = OFFSET_FROM_BIT(frame);
        frames[idx]   &= ~(0x1 << off);
}

/*static uint32_t test_frame(uint32_t frame_addr)
{
        uint32_t frame = frame_addr / 0x1000;
        uint32_t idx   = INDEX_FROM_BIT(frame);
        uint32_t off   = OFFSET_FROM_BIT(frame);
        return frames[idx] & (0x1 << off);
}*/

static uint32_t first_free_frame(void) 
{
        uint32_t i, j;
        for(i = 0; i < INDEX_FROM_BIT(nframes); i++)
                if(frames[i] != 0xFFFFFFFF) /*nothing free*/
                        for(j = 0; j < 32; j++) {
                                uint32_t to_test = 0x1 << j;
                                if(!(frames[i] & to_test))
                                        return i*4*8+j;
                        }
        return 0; /**@todo check if this is correct*/
}

void alloc_frame(page_t *page, int is_kernel, int is_writeable)
{
        uint32_t idx;
        if(page->frame)
                return; /*already allocated*/
        idx = first_free_frame();
        if(idx == (uint32_t)-1)
                panic("No free frames");
        set_frame(idx*0x1000); /*this frame is now ours*/
        page->present = 1;
        page->rw      = is_writeable ? 1: 0;
        page->user    = is_kernel    ? 0: 1;
        page->frame   = idx;
}

void free_frame(page_t *page)
{
        uint32_t frame;
        if(!(frame = page->frame))
                return; /*this page does not give an allocate frame*/
        clear_frame(frame);
        page->frame = 0x0; /*page now does not have a frame*/
}

void initialize_paging(void)
{
        uint32_t mem_end_page = 0x1000000; /*assume we only have 16MiB for the moment*/
        nframes = mem_end_page / 0x1000;
        frames  = (uint32_t*)kmalloc_a(INDEX_FROM_BIT(nframes));
        kmemset(frames, 0, INDEX_FROM_BIT(nframes));
        kernel_directory = (page_directory_t*)kmalloc_a(sizeof(page_directory_t));
        kmemset(kernel_directory, 0, sizeof(*kernel_directory));
        current_directory = kernel_directory;

	monitor_printf("kd %x\n", kernel_directory);

	/* identity map */
        size_t i = 0;
        while(i < 0x1000000)
        {
                /* kernel code is readable but not writeable from user space,
                 * rw flag does not apply to kernel space */
                alloc_frame(get_page(i, 1, kernel_directory), 0, 0);
                i+= 0x1000;
        }

        register_interrupt_handler(14, page_fault);

        switch_page_directory(kernel_directory);
}

void switch_page_directory(page_directory_t *dir)
{
        /*@todo move assembly elsewhere */
        current_directory = dir;
        asm volatile("mov %0, %%cr3" :: "r"(&dir->tables_physical));
        uint32_t cr0;
        asm volatile("mov %%cr0, %0" :  "=r"(cr0));
        cr0 |= 0x80000000;
        asm volatile("mov %0, %%cr0" :: "r"(cr0)); /*enable paging*/
}

page_t *get_page(uint32_t address, int make, page_directory_t *dir)
{
        uint32_t table_idx;
        address /= 0x1000; /* turn address into index */
        /* find the page table containing this address */
        table_idx = address / 1024; 
        if(dir->tables[table_idx]) {
		//monitor_printf("exists: %x\n", dir->tables[table_idx]);
                return &dir->tables[table_idx]->pages[address%1024];
        } else if(make) {
                uint32_t tmp;
                dir->tables[table_idx] = (page_table_t*)kmalloc_ap(sizeof(page_table_t), &tmp);
		kmemset(dir->tables[table_idx], 0, sizeof(page_table_t));
		monitor_printf("make: %x %x %x\n", address*0x1000, tmp, dir->tables[table_idx]);
                dir->tables_physical[table_idx] = tmp | 0x7; /*present, rw, user*/
                return &dir->tables[table_idx]->pages[address%1024];
        }
        return 0;       
}

void page_fault(registers_t *regs)
{
        /*@todo move assembly elsewhere*/
        uint32_t faulting_address;
        asm volatile("mov %%cr2, %0" : "=r"(faulting_address)); /*cr2 contains faulting address*/

        /*error code gives us details of what has happened*/
        int present  = !(regs->error_code & 0x1); /*page not present*/
        int rw       =   regs->error_code & 0x2;  /*write operation?*/
        int us       =   regs->error_code & 0x4;  /*processor was in user mode?*/
        int reserved =   regs->error_code & 0x8;  /*overwritten CPU-reserved bits of page entry?*/
      /*int id       =   regs.error_code & 0x10; // caused by an instruction fetch?*/

        monitor_puts("(error 'page-fault ");
        if(present)  monitor_puts("present ");
        if(rw)       monitor_puts("read-only ");
        if(us)       monitor_puts("user-mode ");
        if(reserved) monitor_puts("reserved ");
        monitor_printf(" %x)\n", faulting_address); 

        panic("Halting due to page fault\n");
}

