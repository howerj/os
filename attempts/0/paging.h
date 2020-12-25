#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "isr.h"

typedef struct {
	uint32_t present:1;	/* page present in memory */
	uint32_t rw:1;		/* read only if clear, read-write if set */
	uint32_t user:1;	/* supervisor level only if clear */
	uint32_t accessed:1;	/* has the page been accessed since last refresh */
	uint32_t dirty:1;	/* has the page been written to since last access */
	uint32_t unused:7;	/* amalgamation of unused and reserved bits */
	uint32_t frame:20;	/* frame address (shifted right 12 bits) */
} page_t;

typedef struct {
	page_t pages[1024];
} page_table_t;

typedef struct {
	page_table_t *tables[1024];
	/*array of *physical* pointers to pages tables, for loading into
	 *the CR3 register*/
	uint32_t tables_physical[1024];
	/*the physical address of tables_physical */
	uint32_t physical_address;
} page_directory_t;

void initialize_paging(void);
void switch_page_directory(page_directory_t * newdir);
page_t *get_page(uint32_t address, int make, page_directory_t * dir);
void page_fault(registers_t * regs);	/*page fault handler */

#endif
