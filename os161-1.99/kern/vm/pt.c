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
#endif /* OPT-A3 */
