#include "opt-A3.h"
#ifdef OPT_A3
#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <mips/tlb.h>
#include <proc.h>
#include <kern/errno.h>
#include <current.h>

/*
 * get the corresponding physical address by passing in a virtual address
 */

int
pt_getEntry(vaddr_t vaddr, paddr_t* paddr, int* segment_type){

	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
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

	//error check
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	//get the base and top address for each segment
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	//text segment
	if(vaddr >= vbase1 && vaddr < vtop1){
		int index = (vaddr - vbase1) / PAGE_SIZE;
		*paddr = as->text_pt[index];
		*segment_type = 0;
		return 0;
	}

	//data segment
	else if(vaddr >= vbase2 && vaddr < vtop2){
		int index = (vaddr - vbase2) / PAGE_SIZE;
		*paddr = as->data_pt[index];
		*segment_type = 1;
		return 0;
	}

	//stack segment
	else if(vaddr >= stackbase && vaddr < stacktop){
		int index = (vaddr - vbase2) / PAGE_SIZE;
		*paddr = as->stack_pt[index];
		*segment_type = 2;
		return 0;
	}

	//error
	else{
		return EFAULT;
	}

}



/*
 * after loading a demand page, store the allocated physicall address into the page table
 */

int
pt_setEntry(vaddr_t vaddr, paddr_t paddr){
        vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
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

        //error check
        KASSERT(as->as_vbase1 != 0);
        KASSERT(as->as_npages1 != 0);
        KASSERT(as->as_vbase2 != 0);
        KASSERT(as->as_npages2 != 0);
        KASSERT(as->as_stackpbase != 0);
        KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
        KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
        KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

        //get the base and top address for each segment
        vbase1 = as->as_vbase1;
        vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
        vbase2 = as->as_vbase2;
        vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
        stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
        stacktop = USERSTACK;

	//allpcate the physicall address for a page, this page is valid
	paddr |= PT_VALID;

	//text segment
        if(vaddr >= vbase1 && vaddr < vtop1){
                int index = (vaddr - vbase1) / PAGE_SIZE;
                paddr_t old_paddr = as->text_pt[index];
		if(old_paddr & PT_READ){
			paddr |= PT_READ;
		}
		if(old_paddr & PT_WRITE){
			paddr |= PT_WRITE;
		}
		if(old_paddr & PT_EXE){
			paddr |= PT_EXE;
		}
		as->text_pt[index] = paddr;
		return 0;
        }

        //data segment
        else if(vaddr >= vbase2 && vaddr < vtop2){
                int index = (vaddr - vbase2) / PAGE_SIZE;
		paddr_t old_paddr = as->data_pt[index];
		if(old_paddr & PT_READ){
			paddr |= PT_READ;
		}
		if(old_paddr & PT_WRITE){
			paddr |= PT_WRITE;
		}
		if(old_paddr & PT_EXE){
			paddr |= PT_EXE;
		}
                as->data_pt[index] = paddr;
		return 0;
        }

        //stack segment
        else if(vaddr >= stackbase && vaddr < stacktop){
		int index = (vaddr - vbase2) / PAGE_SIZE;
		paddr_t old_paddr = as->stack_pt[index];
		if(old_paddr & PT_READ){
			paddr |= PT_READ;
		}
		if(old_paddr & PT_WRITE){
			paddr |= PT_WRITE;
		}
		if(old_paddr & PT_EXE){
			paddr |= PT_EXE;
		}
		as->stack_pt[index] = paddr;
		return 0;
        }

        //error
        else{
                return EFAULT;
        }

}

/* use VOP_READ to load a page
*/
int pt_loadPage(vaddr_t vaddr, paddr_t* paddr, struct addrspace *as, int segment_type){
	
	off_t vnode_page_offset;
	size_t readsize = PAGE_SIZE;
	switch(segment_type){
		case 2:
			// stack
			// get page, then zero region
			return 0;
		case 0:
			vnode_page_offset=vaddr - as->as_vbase1 +  as->as_vbase1_offset;
			if(vnode_page_offset+PAGE_SIZE>as->as_vbase1_filesize){
				readsize = as->as_vbase1_filesize-vnode_page_offset;
			}
			break;
		case 1:
			vnode_page_offset=vaddr - as->as_vbase2 + as->as_vbase2_offset;
			if(vnode_page_offset+PAGE_SIZE>as->as_vbase2_filesize){
				readsize = as->as_vbase2_filesize-vnode_page_offset;
			}
			break;
			// text or data
			// calculate offset
	}
	
	struct iovec iov;
	struct uio u;
	int result;

	DEBUG(DB_EXEC, "ELF: Loading %lu bytes to 0x%lx\n", 
	      (unsigned long) filesize, (unsigned long) vaddr);

	iov.iov_ubase = (userptr_t)vaddr; // start of vaddrs
	iov.iov_len = PAGE_SIZE;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = readsize;          // amount to read from the file
	u.uio_offset = vnode_page_offset;
	u.uio_segflg = is_executable ? UIO_USERISPACE : UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = as;

	result = VOP_READ(v, &u);
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

#endif /* OPT-A3 */
