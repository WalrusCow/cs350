#include <segments.h>
#include <vm.h>
#include <addrspace.h>

/*
 * Determine the segment type of a virtual address.
 */
int
get_seg_type(vaddr_t vaddr, struct addrspace* as, seg_type* seg) {
	if (vaddr >= as->text_seg->vbase && vaddr < as->text_seg->vtop)
		*seg = TEXT;
	else if (vaddr >= as->text_seg->vbase && vaddr < as->text_seg->vtop)
		*seg = DATA;
	else if (vaddr >= stackbase && vaddr < stacktop)
		*seg = STACK;
	else
		return EFAULT;
	return 0;
}
