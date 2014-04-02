#include "opt-A3.h"
#if OPT_A3
#include <coremap.h>
#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <proc.h>
#include <kern/errno.h>
#include <current.h>
#include <uw-vmstats.h>
#include <segments.h>
#include <synch.h>

static paddr_t coremaps_base;
static paddr_t coremaps_end;
static unsigned int coremaps_npages;

static struct coremap* coremaps;

//lock for the coremaps, solve the synchronization problem
static struct lock* coremaps_lock = NULL;

//simple page replacement alogrithm
static unsigned int next_victim = 0;
static int coremaps_get_rr_victim(void){
	int victim;

	victim = next_victim;
	next_victim = (next_victim + 1) % coremaps_napges;
	return victim;
}

/*
 * To initialize the lock for coremaps
 */
void coremaps_lock_init(){
	if(coremaps_lock == NULL){
		coresmap_lock = lock_create("coremaps_lock");
		if(coremaps_lock == NULL){
			panic("coremaps_lock created failed\n");
		}
	}
}

/*
 * To initialize the coremaps
 */
void coremaps_init(){
	//get the sizse of free physical address
	ram_getsize(&coremaps_base, &coremaps_end);

	//make sure get the address can devide by the page size;
	coremaps_base /= PAGE_SIZE;
	coremaps_base += 1;
	coremaps_base *= PAGE_SIZE;
	coremaps_end /= PAGE_SIZE;
	coremaps_end -= 1;
	coremaps_end *= PAGE_SIZE;

	//get the number of pages
	coremaps_npages = (coremaps_end - coremaps_base) / PAGE_SIZE;

	//allocate the space for coremaps
	coremaps = (struct coremap*)PADDR_TO_KVADDR(coremaps_base);
	coremaps_base = coremaps_base + coremaps_npages * sizeof(struct coremap);

	//update the base that can be used for paging
	coremaps_base /= PAGE_SIZE;
	coremaps_base += 1;
	coremaps_base *= PAGE_SIZE;

	//update the number of frames in the memory
	coremaps_npages = (coremaps_end - coremaps_base) / PAGE_SIZE;

	//initialze the coremaps
	for(int i = 0; i < coremaps_npages; i++){
		coremaps[i].free = true;
		coremaps[i].cm_as = NULL;
		coremaps[i].cm_vaddr = 0;
		coremaps[i].n = 0;
	}

}

/*
 * To get the pages from coremaps
 */
paddr_t
coremaps_getppages(unsigned long npages, struct addrspace* as, vaddr_t vaddr){
	lock_acquire(coremaps_lock);

	//allocate 1 page
	if(npages == 1){
		//if there exits free pages in the memory
		for(int i = 0; i < coremaps_npages; i++){
			if(coremaps[i].free == true){
				coremaps[i].cm_as = as;
				coremasp[i].cm_vaddr = vaddr;
				coremaps[i].free = false;
				paddr_t paddr;
				paddr = i * PAGE_SIZE + coremaps_base;
				//zero the page
				as_zero_region(paddr, 1);
				return paddr;
			}
		}

		//page replacement
		//get index
		int index = coremaps_get_rr_victim();

		//may be in the middle of a contiguous block
		if(coremaps[index].n != 0){
			while(coremaps[index].n != 1){
				index = coremaps_get_rr_victim();
			}
			index = coremaps_get_rr_victim();
		}

		//free this physical page first
		paddr_t paddr = index * PAGE_SIZE + coremaps_base;
		coremaps_free(paddr_t paddr);

		//allocate the page
		coremaps[index].cm_as = as;
		coremaps[index].cm_vaddr = vaddr;
		cpremaps[index].free = false;

		as_zero_region(paddr, 1);
		return paddr;
	}

	//allocate more than 1 page at one time
	else{
		bool empty_block = false;
		unsigned int block_size = 0;
		unsigned int block_index = 0;
		//free page exist
		for(size_t i = 0; i < coremaps_npages; i++){
			if(coremaps[i].free == true){
				//the free pages are consecutive
				if(block_index == i - 1){
					block_index += 1;
					block_size += 1;
					//have enough size
					if(block_size == npages){
						size_t index = block_index - npages;
						paddr_t paddr = index * PAGE_SIZE + coremaps_base;
						//allocate pages
						size_t n = npages;
						while(n != 0){
							coremaps[index].as = as;
							coremaps[index].paddr = paddr;
							coremaps[index].free = false;
							coremaps[index].n = n;
							index = (index + 1) % coremaps_npages;
							n -= 1;
						}
						as_zero_region(paddr, npages);
						return paddr;
					}
					else{
						block_index = i;
					}
				}
				else{
					block_index = i;
					blosck_size = 0;
				}
			}
		}

		//no enough free block with npages size
		//get the start index for a block
		int start_index = coremaps_get_rr_victim();

		//may be in the middle of a block
		if(coremaps[start_index].n != 0){
			while(coremaps[index].n != 1){
				start_index = coremaps_get_rr_victim();
			}
			start_index = coremaps_get_rr_victim();
		}

		//get the end index for a block
		int end_index = start_index + npages;

		//may be in the middle of a block
		if(coremaps[end_index].n != 0){
			while(coremaps[index].n != 1){
				end_index = coremaps_get_rr_victim();
			}
			end_index = coremaps_get_rr_victim();
		}

		paddr_t paddr;
		//free all the framse between start index and end index
		int while_index = start_index;
		while(while_index != end_index){
			//one page
			if(coremaps[while_index].n == 0){
				paddr = while_index * PAGE_SIZE + coremaps_base;
				coremaps_free(paddr);
				while_index = (while_index + 1) % coremaps_npages;
			}
			//one block of more than 1 page, free all pages in that block
			else{
				int n = coremaps[while_index].n;
				paddr = while_index * PAGE_SIZE + coremaps_base;
				coremaps_free(paddr);
				while_index = (while_index + n) & coremaps_npages;
			}
		}

		//allocate space
		while_index = start_index;
		int n = (int)npages;
		while(n != 0){
			coremaps[while_index].cm_as = as;
			coremaps[while_index].cm_vaddr = vaddr;
			coremaps[while_index].free = false;
			coremaps[while_index].n = n;
			while_index = (while_index + 1) % coremaps_npages;
			n -= 1;
		}
		paddr = start_index * PAGE_SIZE + coremaps_base;
		as_zero_region(paddr, npages);
		return paddr;
	}

	lock_release(coremaps_lock);

}

/*
 * To free a page in coremaps
 */
void
coremaps_free(paddr_t paddr){
	lock_acquire(coremaps_lock);

	//find the index first
	int index = (paddr - coremaps_base) / PAGE_SIZE;

	//only free the pages in coremap
	if((index >= 0) && (index < coremaps_end)){
		struct addrspace * as = coremaps[index].cm_as;
		vaddr_t vaddr = coremaps[index].cm_vaddr;
		int n = coremaps[index].n;

		//invalid the page in the page table;
		if(as != NULL){
			pt_invalid(vaddr, as);
		}

		//allocate single page
		if(n == 0){
			//TODO: swap out
			coremaps[index].cm_as = NULL;
			coremaps[index].cm_vaddr = 0;
			coremaps[index].free = true;
			coremaps[index].n = 0;
		}
		//aloocate more than 1 page at the time
		else{
			while(n != 0){
				//TODO: swap out
				coremaps[index].cm_as = NULL;
				coremaps[index].cm_vaddr = 0;
				coremaps[index].free = true;
				coremaps[index].n = 0;
				index = (index + 1) % coremaps_npages;
				n -= 1;
			}
		}
	}


	lock_release(coremaps_lock);
}

/*
 * To free pages in one address space
 */
void
coremaps_as_free(struct addrspace* as){
	// Free memory for all segments from this address space
	for (seg_type type = TEXT; type <= STACK; ++type) {
		paddr_t* pt = get_pt(type, as);
		if (pt == NULL) continue;
		struct segment* seg = get_segment(type, as);
		if (seg == NULL) continue;

		// Iterate over the page table
		for (size_t npages = seg->npages; npages > 0; --npages) {
			paddr_t paddr = pt[npages-1];
			if (paddr & PT_VALID) coremaps_free(paddr & PAGE_FRAME);
		}
	}
}

#endif /* OPT-A3 */
