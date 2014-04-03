#include <coremap.h>
#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <uw-vmstats.h>
#include <segments.h>
#include <synch.h>

static paddr_t coremaps_base;
static paddr_t coremaps_end;
static unsigned int coremaps_npages;

static struct coremap* coremaps;

// lock for the coremaps, solve the synchronization problem
static struct lock* coremaps_lock = NULL;

// simple page replacement alogrithm
static size_t next_victim = 0;
static size_t coremaps_get_rr_victim(void){
	size_t victim;

	victim = next_victim;
	next_victim = (next_victim + 1) % coremaps_npages;
	return victim;
}

/*
 * To initialize the lock for coremaps
 */
void coremaps_lock_init(){
	if(coremaps_lock == NULL){
		coremaps_lock = lock_create("coremaps_lock");
		if(coremaps_lock == NULL){
			panic("coremaps_lock created failed\n");
		}
	}
}

/*
 * To initialize the coremaps
 */
void coremaps_init(){
	// get the size of free physical address
	ram_getsize(&coremaps_base, &coremaps_end);

	// make sure get the address can divide by the page size;
	coremaps_base /= PAGE_SIZE;
	coremaps_base += 1;
	coremaps_base *= PAGE_SIZE;
	coremaps_end /= PAGE_SIZE;
	coremaps_end -= 1;
	coremaps_end *= PAGE_SIZE;

	// get the number of pages
	coremaps_npages = (coremaps_end - coremaps_base) / PAGE_SIZE;

	// allocate the space for coremaps
	coremaps = (struct coremap*)PADDR_TO_KVADDR(coremaps_base);

	coremaps_base = coremaps_base + coremaps_npages * sizeof(struct coremap);

	// update the base that can be used for paging
	coremaps_base /= PAGE_SIZE;
	coremaps_base += 1;
	coremaps_base *= PAGE_SIZE;

	// update the number of frames in the memory
	coremaps_npages = (coremaps_end - coremaps_base) / PAGE_SIZE;

	// initialze the coremaps
	for(size_t i = 0; i < coremaps_npages; i++){
		coremaps[i].free = true;
		coremaps[i].cm_as = NULL;
		coremaps[i].cm_vaddr = 0;
		coremaps[i].npages = 0;
		// Only set to false for kernel
		coremaps[i].swappable = true;
	}

}

/*
 * To get the pages from coremaps
 */
paddr_t
coremaps_getppages(size_t npages, struct addrspace* as, vaddr_t vaddr) {
	lock_acquire(coremaps_lock);

	// Return address
	paddr_t paddr;

	// allocate 1 page
	if (npages == 1) {
		// if there exists free pages in the memory
		for (size_t i = 0; i < coremaps_npages; i++) {
			if (coremaps[i].free) {
				coremaps[i].cm_as = as;
				coremaps[i].cm_vaddr = vaddr;
				coremaps[i].free = false;
				coremaps[i].swappable = (as == NULL);
				paddr = i * PAGE_SIZE + coremaps_base;
				// zero the page
				as_zero_region(paddr, 1);
				lock_release(coremaps_lock);
				return paddr;
			}
		}

		// page replacement
		size_t index = coremaps_get_rr_victim();

		// may be in the middle of a contiguous block
		// TODO: Infinite loop check
		while (!coremaps[index].swappable) {
			index = coremaps_get_rr_victim();
		}

		// free this physical page first
		// TODO: swap out this addr
		paddr_t paddr = index * PAGE_SIZE + coremaps_base;
		coremaps_free(paddr);

		// allocate the page
		coremaps[index].cm_as = as;
		coremaps[index].cm_vaddr = vaddr;
		coremaps[index].free = false;
		coremaps[index].swappable = (as == NULL);

		as_zero_region(paddr, 1);
		lock_release(coremaps_lock);
		return paddr;
	}

	// Allocate more than 1 page at one time

	// First we search for a contiguous block of free pages
	// Index that the free block starts at
	size_t block_index = 0;
	// Size of the free block discovered so far
	size_t block_size = 0;

	// free page exist
	// TODO: Also track the *maximum* length free block, for more efficient
	// swapping if there is no free block of the proper size
	// TODO: Also when being smart consider moving pages around (but
	// we cannot move part of a contiguous block)
	for(size_t i = 0; i < coremaps_npages; i++){
		// Not free page - reset the block
		if (!coremaps[i].free) {
			block_index = 0;
			block_size = 0;
			continue;
		}

		// Another free page in a row
		block_size += 1;
		// Only update the start if this is the first free page
		block_index = (block_index == 0) ? i : block_index;

		// We have found a proper contiguous block!
		if (block_size == npages) break;
	}

	if (block_size == npages) {
		// If we found a contiguous block, allocate it and quit
		for (size_t i = 0; i < block_size; ++i) {
			// Allocate the page at block_index + i for this segment
			coremaps[block_index + i].cm_as = as;
			coremaps[block_index + i].cm_vaddr = vaddr;
			coremaps[block_index + i].free = false;
			// TODO: We will set to 0 for error checking
			coremaps[block_index + i].npages = block_size;
			coremaps[i].swappable = (as == NULL);

			// Next page should have next page virtual address
			vaddr += PAGE_SIZE;
		}

		// Special first one has npages equal to block size
		// TODO: Once 0 for error checking this is relevant
		coremaps[block_index].npages = block_size;

		// Compute the physical address from the block start position
		paddr = block_index * PAGE_SIZE + coremaps_base;
		as_zero_region(paddr, npages);

		lock_release(coremaps_lock);
		return paddr;
	}

	// We did not find a contiguous free block of the correct size
	// therefore, we must search for a block of the correct size made
	// up of free pages or swappable pages (not kmalloc'd)

	// TODO: Do a loop like above BUT change the exit condition
	// to be if free *OR* user space, since we cannot ever swap
	// kernel pages out. If not found then error.
	// ... we can pass in a check function!!! lol, this seems ugly either way

	// TODO: looop like before!!!
	block_size = 0;
	block_index = coremaps_get_rr_victim();

	// free all the frames between start index and end index
	// TODO: Make this a function, with npages as arg (1 for top)
	/*
	 * NOTE: We have a contiguous block of non-kernel pages at this point
	 */
	for (size_t i = 0; i <= block_size; ++i) {
		// Shorthand
		struct coremap* page = coremaps + block_index + i;
		//if (!page.free) {
		//	// TODO: Swap out
		//}

		// Allocate the page at block_index + i for this segment
		page->cm_as = as;
		page->cm_vaddr = vaddr;
		page->free = false;
		// TODO: We will set to 0 for error checking
		page->npages = block_size;
		page->swappable = (as == NULL);

		// Next page should have next page virtual address
		vaddr += PAGE_SIZE;
	}
	// TODO: Once we set to 0 this becomes relevant
	coremaps[block_index].npages = block_size;

	paddr = block_index * PAGE_SIZE + coremaps_base;
	as_zero_region(paddr, npages);

	lock_release(coremaps_lock);
	return paddr;
}

/*
 * To free a page in coremaps
 */
void
coremaps_free(paddr_t paddr){
	// TODO: Should this be allowed, or panic'd?
	if (paddr > coremaps_base) return;

	lock_acquire(coremaps_lock);

	KASSERT(paddr == (paddr & PAGE_FRAME));

	// find the index first
	size_t index = (paddr - coremaps_base) / PAGE_SIZE;

	// Don't try to free pages not in the map (this shouldn't happen?)
	// TODO: KASSERT this?
	if (index >= coremaps_npages) {
		lock_release(coremaps_lock);
		return;
	}

	// How many pages were allocated at the same time as this one
	// TODO: All but first have npages 0 is better
	size_t npages = coremaps[index].npages;
	struct addrspace* as = coremaps[index].cm_as;

	if (npages == 0) npages = 1;
	// TODO: This should be here but the if above shouldn't
	KASSERT(npages > 0);

	// Free all pages that were allocated
	for (; npages > 0; --npages) {
		KASSERT(index < coremaps_npages);

		if (as != NULL) {
			// Invalidate the page in the page table (not for kernel though)
			vaddr_t vaddr = coremaps[index].cm_vaddr;
			pt_invalid(vaddr, as);
		}
		coremaps[index].cm_as = NULL;
		coremaps[index].cm_vaddr = 0;
		coremaps[index].free = true;
		coremaps[index].npages = 0;
		index += 1;
	}

	lock_release(coremaps_lock);
}

/*
 * To free pages in one address space
 */
void
coremaps_as_free(struct addrspace* as) {
	// Free memory for all segments from this address space
	for (seg_type type = TEXT; type <= STACK; ++type) {
		struct pte* pt = get_pt(type, as);
		if (pt == NULL) continue;
		struct segment* seg = get_segment(type, as);
		if (seg == NULL) continue;

		// Iterate over the page table
		for (size_t npages = seg->npages; npages > 0; --npages) {
			paddr_t paddr = pt[npages-1].paddr;
			if (paddr & PT_VALID) coremaps_free(paddr & PAGE_FRAME);
		}
	}
}
