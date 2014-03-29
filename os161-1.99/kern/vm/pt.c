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

paddr_t
pt_getEntry(vaddr_t vaddr, paddr_t* paddr){
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	struct addrspace *as;

	as = curproc_getas();

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
		return 0;
	}

	//data segment
	else if(vaddr >= vbase2 && vaddr < vtop2){
		int index = (vaddr - vbase2) / PAGE_SIZE;
		*paddr = as->data_pt[index];
		return 0;
	}

	//stack segment
	else if(vaddr >= stackbase && vaddr < stacktop){
		int index = (vaddr - vbase2) / PAGE_SIZE;
		*paddr = as->stack_pt[index];
		return 0;
	}

	//error
	else{
		return EFAULT;
	}

}

int
pt_setEntry(vaddr_t vaddr, paddr_t paddr){
        vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	struct addrspace *as;

	as = curproc_getas();

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
                as->text_pt[index] = paddr;
		return 0;
        }

        //data segment
        else if(vaddr >= vbase2 && vaddr < vtop2){
                int index = (vaddr - vbase2) / PAGE_SIZE;
                as->data_pt[index] = paddr;
		return 0;
        }

        //stack segment
        else if(vaddr >= stackbase && vaddr < stacktop){
                int index = (vaddr - vbase2) / PAGE_SIZE;
                as->stack_pt[index] = paddr;
		return 0;
        }

        //error
        else{
                return EFAULT;
        }


}
#endif /* OPT-A3 */
