#include "opt-A3.h"
#if OPT_A3
#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <mips/tlb.h>
#include <proc.h>
#include <kern/errno.h>
#include <current.h>
#include <uio.h>
#include <vnode.h>


/*
 * get the corresponding physical address by passing in a virtual address
 */

int
pt_getEntry(vaddr_t vaddr, paddr_t* paddr, int* segment_type){

	struct addrspace *as;

	//we only care first 20 bits, the page number
	vaddr &= PAGE_FRAME;

	//should get a valid as from a valid process
	if(curproc == NULL){
		return EFAULT;
	}

	as = curproc_getas();
	if(as == NULL){
		return EFAULT;
	}

	vaddr_t vbase;
	paddr_t* pageTable = pt_getTable(vaddr, as, segment_type, &vbase);

	if (pageTable == NULL) {
		return EFAULT;
	}

	int index = (vaddr - vbase) / PAGE_SIZE;
	*paddr = pageTable[index];
	return 0;

}

/*
 * after loading a demand page, store the allocated
 * physical address into the page table
 */

int
pt_setEntry(vaddr_t vaddr, paddr_t paddr){
	struct addrspace *as;

	//we only care the page number and frame number
	vaddr &= PAGE_FRAME;
	paddr &= PAGE_FRAME;

	if(curproc == NULL){
		return EFAULT;
	}

	as = curproc_getas();
	if(as == NULL){
		return EFAULT;
	}

	//allocate the physical address for a page, this page is valid
	paddr |= PT_VALID;

	vaddr_t base_vaddr;
	int segType;
	paddr_t* pageTable = pt_getTable(vaddr, as, &segType, &base_vaddr);

	if (pageTable == NULL) {
		return EFAULT;
	}

	// Index in the page table
	int index = (vaddr - base_vaddr) / PAGE_SIZE;
	// Keep all old flags (they are initialized at start)
	paddr |= pageTable[index] & ~PAGE_FRAME;
	pageTable[index] = paddr;
	return 0;

}

/* use VOP_READ to load a page
*/
int 
pt_loadPage(vaddr_t vaddr, struct addrspace *as, int segment_type){
	off_t vnode_page_offset;
	size_t readsize = PAGE_SIZE;
	
	switch(segment_type){
		case 2:
			// does nothing for stack
			return 0;
		case 0:
			vnode_page_offset = vaddr - as->as_vbase1 +  as->as_vbase1_offset;
			if(vnode_page_offset + PAGE_SIZE > as->as_vbase1_filesize){
				readsize = as->as_vbase1_filesize - vnode_page_offset;
			}
			break;
		case 1:
			vnode_page_offset = vaddr - as->as_vbase2 + as->as_vbase2_offset;
			if(vnode_page_offset + PAGE_SIZE > as->as_vbase2_filesize){
				readsize = as->as_vbase2_filesize - vnode_page_offset;
			}
			break;
			// text or data
			// calculate offset
	}

	struct iovec iov;
	struct uio u;
	int result;

	iov.iov_ubase = (userptr_t)vaddr; // start of vaddrs
	iov.iov_len = PAGE_SIZE;		 // length of the memory space

	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = readsize;          // amount to read from the file
	u.uio_offset = vnode_page_offset;
	// Only executable if in text segment
	u.uio_segflg = segment_type ? UIO_USERSPACE : UIO_USERISPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = as;

	result = VOP_READ(as->as_vn, &u);
	if (result) {
		return result;
	}

	if (u.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on segment - file truncated?\n");
		return ENOEXEC;
	}

	/*
	 * If memsize > filesize, the remaining space should be
	 * zero-filled. There is no need to do this explicitly,
	 * because the VM system should provide pages that do not
	 * contain other processes' data, i.e., are already zeroed.
	 *
	 * During development of your VM system, it may have bugs that
	 * cause it to (maybe only sometimes) not provide zero-filled
	 * pages, which can cause user programs to fail in strange
	 * ways. Explicitly zeroing program BSS may help identify such
	 * bugs, so the following disabled code is provided as a
	 * diagnostic tool. Note that it must be disabled again before
	 * you submit your code for grading.
	 */
#if 0
	{
		size_t fillamt;

		fillamt = memsize - filesize;
		if (fillamt > 0) {
			DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n", 
			      (unsigned long) fillamt);
			u.uio_resid += fillamt;
			result = uiomovezeros(fillamt, &u);
		}
	}
#endif
	
	return result;
}

/*
 * Get the table for this vaddr, or NULL if doesn't exist.
 * Also store the segment type in segType, and the segment base in base.
 */
paddr_t*
pt_getTable(vaddr_t vaddr, struct addrspace* as, int* segType, vaddr_t* vbase) {

	//error check
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);

	KASSERT(segType); // Not NULL, please

	// All the virtual addresses
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;

	// Get the base and top address for each segment
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	// vaddr is from text segment
	if(vaddr >= vbase1 && vaddr < vtop1){
		*vbase = vaddr - vbase1;
		*segType = 0;
		return as->text_pt;
	}

	// vaddr is from data segment
	else if(vaddr >= vbase2 && vaddr < vtop2){
		*vbase = vaddr - vbase2;
		*segType = 1;
		return as->data_pt;
	}

	// vaddr is from stack
	else if(vaddr >= stackbase && vaddr < stacktop){
		*vbase = vaddr - stackbase;
		*segType = 2;
		return as->stack_pt;
	}

	// Invalid virtual address
	return NULL;
}

#endif /* OPT-A3 */
