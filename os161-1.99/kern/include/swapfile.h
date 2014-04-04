#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

#include "opt-A3.h"
#if OPT_A3
#include <types.h>
#include <addrspace.h>

// Size in bytes
#define SWAPFILE_SIZE 9*1024*1024
// Number of pages in the file
#define SWAPFILE_PAGES SWAPFILE_SIZE / PAGE_SIZE

#define SWAPFILE_NAME "/SWAPFILE"

void swap_free(uint16_t pageIndex);

void swap_init(void);

void swap_destroy(void);

int swapin_mem(uint16_t pageIndex, paddr_t p_dest);

/*
	return the offset in the swap file if success
*/
int swapout_mem(paddr_t paddr, uint16_t *swap_page);
#endif

#endif
