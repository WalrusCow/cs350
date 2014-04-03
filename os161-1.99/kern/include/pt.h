#ifndef _PT_H_
#define _PT_H_

#include "opt-A3.h"

#if OPT_A3
#include <segments.h>
#include <vm.h>

#define PT_VALID 0x00000200
#define PT_DIRTY 0x00000400

#define PT_READ 0X00000080
#define PT_WRITE 0x00000040
#define PT_EXE 0x00000020

//entry in the page table
struct pte{
	//frame number for the current page
	paddr_t paddr;
	//offset in swap file, 16 bits is enough since the swap file is 9MB maximum
	//0xffff means not in swap file
	int16_t swap_offset;
};

/*
 * get the corresponding physical address by passing in a virtual address
 */
int
pt_getEntry(vaddr_t vaddr, paddr_t* paddr);

/*
 * after loading a demand page, store the allocated
 * physical address into the page table
 */
int
pt_setEntry(vaddr_t vaddr, paddr_t paddr);

/*
 * use VOP_READ to load a page
 */
int
pt_loadPage(vaddr_t vaddr, paddr_t paddr, struct addrspace *as, seg_type type);

/*
 * Get the page table for this vaddr, or NULL if doesn't exist.
 */
struct pte *
get_pt(seg_type type, struct addrspace* as);

/*
 * Invalid one entry in the page table
 */
void pt_invalid(vaddr_t vaddr, struct addrspace* as);
#endif /* OPT-A3 */

#endif /* _PT_H_ */
