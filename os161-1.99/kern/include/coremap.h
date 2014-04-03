#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <types.h>

struct addrspace;

// entry in the coremap table
struct coremap {
	// record is this page allocated from get 1 pages or get more than 1 pages
	// 0 is for one page, n for allocated from get n pages,
	// where n is greater than 1
	// TODO: Change this to be 1 if 1...
	size_t npages;
	// as pointer
	struct addrspace* cm_as;
	// corresponding vertual address
	vaddr_t cm_vaddr;
	// indicate is the fram allocated or not
	bool free;
};

/*
 * To initialize the lock for coremaps
 */
void coremaps_lock_init(void);

/*
 * To initialize the coremaps
 */
void coremaps_init(void);

/*
 * To get the pages from coremaps
 */
paddr_t
coremaps_getppages(size_t npages, struct addrspace* as, vaddr_t vaddr);

/*
 * To free a page in coremaps
 */
void
coremaps_free(paddr_t paddr);

/*
 * To free pages in one address space
 */
void
coremaps_as_free(struct addrspace* as);

#endif /* _COREMAP_H_ */
