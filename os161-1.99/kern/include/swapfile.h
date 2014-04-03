#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

#include "opt-A3.h"
#if OPT_A3
#include <types.h>
#include <addrspace.h>

#define SWAPFILESIZE 9437184

int swapfree(struct addrspace *as);

void swap_init(void);

void swap_destroy(void);

int swapin_mem(uint16_t file_offset,paddr_t p_dest);

/*
	return the offset in the swap file if success
*/
int swapout_mem(paddr_t paddr,uint16_t *swap_offset);
#endif

#endif
