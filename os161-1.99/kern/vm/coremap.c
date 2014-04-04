#include <coremap.h>
#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <uw-vmstats.h>
#include <segments.h>
#include <synch.h>
#include <swapfile.h>

static paddr_t coremaps_base;
static paddr_t coremaps_end;
static unsigned int cm_npages;

static struct coremap* coremaps;

// lock for the coremaps, solve the synchronization problem
static struct lock* coremaps_lock = NULL;

// simple page replacement alogrithm (TODO)
static size_t next_victim = 0;
static size_t cm_get_victim(void){
	size_t victim;

	victim = next_victim;
	next_victim = (next_victim + 1) % cm_npages;
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
	cm_npages = (coremaps_end - coremaps_base) / PAGE_SIZE;

	// allocate the space for coremaps
	coremaps = (struct coremap*)PADDR_TO_KVADDR(coremaps_base);

	coremaps_base = coremaps_base + cm_npages * sizeof(struct coremap);

	// update the base that can be used for paging
	coremaps_base /= PAGE_SIZE;
	coremaps_base += 1;
	coremaps_base *= PAGE_SIZE;

	// update the number of frames in the memory
	cm_npages = (coremaps_end - coremaps_base) / PAGE_SIZE;

	// initialze the coremaps
	for(size_t i = 0; i < cm_npages; i++){
		coremaps[i].free = true;
		coremaps[i].cm_as = NULL;
		coremaps[i].cm_vaddr = 0;
		coremaps[i].npages = 0;
	}
}

/*
 * Return true if the page is swappable or free.
 */
static
bool
check_free_swap(struct coremap* pg) {
	// Swappable if address space is not NULL
	return (pg->cm_as != NULL) || pg->free;
}

/*
 * Return true if the page is free.
 */
static
bool
check_free(struct coremap* pg) {
	return pg->free;
}

/*
 * Find a contiguous region where all pages return true for
 * the specified function. Return an error if no such region
 * is found.
 *
 * Store the index of the start of the region in the pointer specified by
 * `region_idx`.
 *
 * It searches beginning at startidx, and goes around up to twice.
 * We go around twice (actually just starting at startidx until we go through
 * the whole map *in order*) because otherwise we might miss a contiguous
 * segment of the correct length that has `startidx` in its middle.
 */
static
int
cm_findRegion(size_t startidx, size_t len, size_t* region_idx,
		bool (*check)(struct coremap*)) {

	KASSERT(region_idx);

	// Index that the free block starts at
	size_t block_index = 0;
	// Size of the free block discovered so far
	size_t block_size = 0;

	// We start looking at a specified index
	size_t idx = startidx;

	// Flag to indicate if we have wrapped around the map yet
	bool wrap = false;

	for(size_t i = 0; i < cm_npages || !wrap; ++i, idx = (idx + 1) % cm_npages){
		// If we just wrapped around, then reset the block, because
		// it must be contiguous memory :)
		if (idx == 0) {
			wrap = true;
			block_size = 0;
			block_index = 0;
		}

		// Not free page - reset the block and skip
		if (!check(coremaps + idx)) {
			block_size = 0;
			block_index = 0;
			continue;
		}

		// Another free page in a row
		block_size += 1;
		// Only update the start if this is the first free page
		block_index = (block_size == 1) ? idx : block_index;

		// We have found a proper contiguous block!
		if (block_size == len) {
			*region_idx = block_index;
			return 0;
		}
	}
	return -1;
}

/*
 * Allocate a specified region.
 * Swaps out all required pages in the specified region.
 */
static
paddr_t
cm_allocRegion(size_t start, size_t len, struct addrspace* as, vaddr_t vaddr) {
	for (size_t idx = start; idx < start + len; ++idx) {
		KASSERT(idx < cm_npages);

		// Shorthand
		struct coremap* page = coremaps + idx;
		// Must be free or swappable
		KASSERT(page->free || page->cm_as);

		if (!page->free) {

			uint16_t swap_offset = 0xffff;
			paddr_t paddr = coremaps_base + (PAGE_SIZE*idx);
			seg_type type;
			// Trust that there is no error
			get_seg_type(page->cm_vaddr, page->cm_as, &type);
			// Do not swap out the text segment (it is read only)
			if(type != TEXT) {
				swapout_mem(paddr, &swap_offset); // set the offset
			}

			// Invalidate the page in the page table
			pt_invalid(page->cm_vaddr, page->cm_as, swap_offset);
		}

		// Allocate the page at block_index + i for this segment
		page->cm_as = as;
		page->cm_vaddr = vaddr;
		page->free = false;
		page->npages = 0;

		// Next page should have next page virtual address
		vaddr += PAGE_SIZE;
	}

	// First page set must record the number of pages set
	coremaps[start].npages = len;

	// Determine the page and zero it
	paddr_t paddr = start * PAGE_SIZE + coremaps_base;
	as_zero_region(paddr, len);
	return paddr;
}

/*
 * To get the pages from coremaps
 */
paddr_t
coremaps_getppages(size_t npages, struct addrspace* as, vaddr_t vaddr) {

	lock_acquire(coremaps_lock);

	// Find the page(s) that we will take over
	size_t idx;
	size_t startidx = 0;
	// Check for a region of free pages
	int err = cm_findRegion(startidx, npages, &idx, check_free);

	// We didn't find a region of free pages - search for a region we can swap
	if (err) {
		startidx = cm_get_victim();
		err = cm_findRegion(startidx, npages, &idx, check_free_swap);
		if (err) {
			lock_release(coremaps_lock);
			return err;
		}
	}

	// Allocate the region
	paddr_t paddr = cm_allocRegion(idx, npages, as, vaddr);
	lock_release(coremaps_lock);
	return paddr;
}

/*
 * To free a page in coremaps. Also invalidate the PT and TLB if necessary.
 */
void
coremaps_free(paddr_t paddr) {
	// TODO: Should this be allowed, or panic'd?
	if (paddr < coremaps_base) return;

	KASSERT(paddr == (paddr & PAGE_FRAME));

	// find the index first
	size_t index = (paddr - coremaps_base) / PAGE_SIZE;

	// Don't try to free pages not in the map (this shouldn't happen?)
	// TODO: KASSERT this?
	if (index >= cm_npages) {
		return;
	}

	lock_acquire(coremaps_lock);

	// How many pages were allocated at the same time as this one
	size_t npages = coremaps[index].npages;
	struct addrspace* as = coremaps[index].cm_as;

	KASSERT(npages > 0);

	// Free all pages that were allocated
	for (; npages > 0; --npages) {
		KASSERT(index < cm_npages);

		if (as != NULL) {
			// Invalidate the page in the page table (not for kernel though)
			vaddr_t vaddr = coremaps[index].cm_vaddr;
			pt_invalid(vaddr, as, 0xffff);
		}
		// Invalidate the coremap entry
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
		for (size_t i = 0; i < seg->npages; ++i) {
			paddr_t paddr = pt[i].paddr;
			uint16_t offset = pt[i].swap_offset;
			if (paddr & PT_VALID) {
				coremaps_free(paddr & PAGE_FRAME);
			}
			else if(offset != 0xffff) {
				swap_free(offset);
			}
		}
	}
}
