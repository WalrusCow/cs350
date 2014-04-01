#include <pt.h>
#include <vm.h>
#include <addrspace.h>
#include <segments.h>

/*
 * Get the index of the virtual address in the specified page
 * table for the given address space.
 */
static
int
get_pt_index(seg_type segType, vaddr_t vaddr, struct addrspace* as) {
	vaddr_t vbase;
	if (segType == STACK) {
		vbase = STACK_BASE;
	}
	else {
		struct segment* seg = get_segment(segType, as);
		vbase = seg->vbase;
	}
	return (vaddr - vbase) / PAGE_SIZE;
}

/*
 * Set the page table entry specified by vaddr to be paddr,
 * with all appropriate flags.
 */
void
pt_setEntry(paddr_t paddr, vaddr_t vaddr, seg_type seg, struct addrspace* as) {
	// The table in question
	struct pte* table = get_pt(seg, as);

	// Index in the table
	int idx = get_pt_index(seg, vaddr, as);

	// Turn valid bit on; keep old flags
	table[idx] = paddr | (table[idx] & ~PAGE_FRAME) | PT_VALID;
}

/*
 * Return a pointer to a page table entry.
 */
struct pte*
pt_getEntry(vaddr_t vaddr, seg_type seg, struct addrspace* as) {
	// The table in question
	struct pte* table = get_pt(seg, as);

	// Index in table
	int idx = get_pt_index(seg, vaddr, as);

	// Return a pointer
	return table + idx;
}

/*
 * Retrieve the appropriate page table for the virtual address
 */
struct pte*
get_pt(seg_type seg, vaddr_t vaddr, struct addrspace* as) {
	// Retrieve the appropriate page table for the virtual address
	switch(seg) {
		case TEXT:
			return as->text_pt;
		case DATA:
			return as->data_pt;
		case STACK:
			return as->stack_pt;
	}
	return NULL; // Just in case
}
