#include <types.h>
#include <vm.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <segments.h>
#include <lib.h>

// Get the type of the vaddr, return error if so
int
get_seg_type(vaddr_t vaddr, struct addrspace* as, seg_type* type) {
	//error check
	KASSERT(type);

	if (vaddr >= as->text_seg->vbase && vaddr < as->text_seg->vtop)
		*type = TEXT;
	else if (vaddr >= as->data_seg->vbase && vaddr < as->data_seg->vtop)
		*type = DATA;
	else if (vaddr >= STACK_BASE && vaddr < USERSTACK)
		*type = STACK;
	else
		return EFAULT;
	return 0;
}

struct segment*
get_segment(seg_type type, struct addrspace* as) {
	switch(type) {
		case TEXT:
			return as->text_seg;
		case DATA:
			return as->data_seg;
		case STACK:
			// Recall: This segment not in ELF file
			return as->stack_seg;
	}
	return NULL;
}

struct segment*
seg_create(seg_type type, off_t offset, size_t filesz, size_t sz,
		vaddr_t vbase) {

	struct segment* seg = kmalloc(sizeof(struct segment));
	if (seg == NULL) return NULL;

	/* Align the region. First, the base... */
	sz += vbase & ~(vaddr_t)PAGE_FRAME;
	vbase &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	seg->vbase = vbase;
	seg->vtop = vbase + sz;
	seg->type = type;
	seg->filesize = filesz;
	seg->file_offset = offset;
	seg->npages = (seg->vtop - seg->vbase) / PAGE_SIZE;

	return seg;
}
