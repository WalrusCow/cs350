#include "opt-A3.h"
#if OPT_A3
#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <proc.h>
#include <kern/errno.h>
#include <current.h>
#include <uio.h>
#include <vnode.h>
#include <uw-vmstats.h>
#include <mips/vm.h>
#include <segments.h>
#include <swapfile.h>

/*
 * get the corresponding physical address by passing in a virtual address
 * TODO: Take in address space.
 */
int pt_getEntry(vaddr_t vaddr, struct pte* PTE) {

	struct addrspace *as;

	// we only care first 20 bits, the page number
	vaddr &= PAGE_FRAME;

	// should get a valid as from a valid process
	if (curproc == NULL) return EFAULT;

	as = curproc_getas();
	if (as == NULL) return EFAULT;

	seg_type type;
	int err = get_seg_type(vaddr, as, &type);
	if (err) return err;

	struct segment* seg = get_segment(type, as);
	struct pte* pageTable = get_pt(type, as);

	// TODO: Account for stack
	int index = (vaddr - seg->vbase) / PAGE_SIZE;
	*PTE = pageTable[index];
	return 0;
}

/*
 * after loading a demand page, store the allocated
 * physical address into the page table
 * TODO: Take in address space.
 */

int
pt_setEntry(vaddr_t vaddr, paddr_t paddr) {
	struct addrspace *as;

	// we only care the page number and frame number
	vaddr &= PAGE_FRAME;
	paddr &= PAGE_FRAME;

	if(curproc == NULL) return EFAULT;

	as = curproc_getas();
	if(as == NULL) return EFAULT;

	// We just allocated it, so this is obviously valid
	// TODO: We actually want to do this outside ? Otherwise how to *invalidate*
	// Also, we probably want a bitmask for the options without
	// the valid bit :)
	paddr |= PT_VALID;

	seg_type type;
	int err = get_seg_type(vaddr, as, &type);
	if (err) return err;

	struct segment* seg = get_segment(type, as);
	struct pte* pageTable = get_pt(type, as);

	// Index in the page table
	int index = (vaddr - seg->vbase) / PAGE_SIZE;
	// Keep all old flags (they are initialized at start)
	paddr |= (pageTable[index].paddr) & ~PAGE_FRAME;
	pageTable[index].paddr = paddr;
	pageTable[index].swap_offset = 0xffff;
	return 0;
}

/*
 * use VOP_READ to load a page
 */
int
pt_loadPage(vaddr_t vaddr, paddr_t paddr, uint16_t swap_offset, struct addrspace *as, seg_type type) {
	if (type == STACK) {
		// Does nothing for stack (page already zeroed)
		vmstats_inc(VMSTAT_PAGE_FAULT_ZERO);
		return 0;
	}

	vmstats_inc(VMSTAT_PAGE_FAULT_DISK);

	if(swap_offset != 0xffff){
		// load from swapfile
		int result = swapin_mem(swap_offset,paddr);
		vmstats_inc(VMSTAT_SWAP_FILE_READ);
		return result;
	}

	struct segment* seg = get_segment(type, as);

	// Offset into the segment
	size_t seg_offset = vaddr - seg->vbase;
	// Number of bytes left in the segment
	size_t bytes_left;
	// Don't read anything if we are at an address past the
	// last actual data in the ELF file. This can happen because
	// memsize > filesize in the ELF header.
	if (seg_offset > seg->filesize)
		bytes_left = 0;
	else
		bytes_left = (seg->filesize - seg_offset);
	// We want to read the minimum of remaining bytes and the size of a page
	size_t readsize = (bytes_left < PAGE_SIZE) ? bytes_left : PAGE_SIZE;

	vmstats_inc(VMSTAT_ELF_FILE_READ);

	// Don't bother executing the read call if we're not reading anything
	// TODO: Does this count as a "Zero" stat?
	if (readsize == 0) return 0;

	/*
	 * We are pretending that we are writing to kernel space even though
	 * we're writing to the physical address of a user space virtual address.
	 * This is a bit of a hack, but it is necessary so that we can't TLB fault
	 * in this function, since this is called from within the fault
	 * handler itself.
	 */

	struct iovec iov;
	struct uio u;

	// Net file offset, as opposed to segment file offset
	size_t file_offset = seg_offset + seg->file_offset;
	void* kvaddr = (void*)PADDR_TO_KVADDR(paddr);
	uio_kinit(&iov, &u, kvaddr, readsize, file_offset, UIO_READ);

	int result = VOP_READ(as->as_vn, &u);
	if (result) return result;

	if (u.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on segment - file truncated?\n");
		return ENOEXEC;
	}

	return 0;
}

/*
 * Get the page table for this vaddr, or NULL if doesn't exist.
 */
struct pte*
get_pt(seg_type type, struct addrspace* as) {

	switch(type) {
		case TEXT:
			return as->text_pt;
		case DATA:
			return as->data_pt;
		case STACK:
			return as->stack_pt;
	}
	// Invalid virtual address
	return NULL;
}

/*
 * Invalid one entry in the page table
`* meanwhile, invalidate tlb if applicable
 */
void
pt_invalid(vaddr_t vaddr, struct addrspace* as,uint16_t swap_offset){

	KASSERT(as != NULL);

	//get the table
	seg_type type;
	get_seg_type(vaddr, as, &type);

	struct segment* seg = get_segment(type, as);
	struct pte* pageTable = get_pt(type, as);

	// Index in the page table
	int index = (vaddr - seg->vbase) / PAGE_SIZE;

	// invalid that entry
	paddr_t paddr = pageTable[index].paddr;
	paddr &= ~(PT_VALID);
	pageTable[index].paddr = paddr;
	pageTable[index].swap_offset = swap_offset; // invalid and swap out

	// we don't know if its in tlb
	if(curproc != NULL && curproc_getas()==as){
		struct tlbshootdown tlbs;
		tlbs.ts_vaddr = vaddr;
		vm_tlbshootdown(&tlbs);
	}

}

struct pte*
create_pt(size_t npages, int flags) {
	struct pte* pt = kmalloc(sizeof(struct pte) * npages);
	if (pt == NULL) return NULL;

	for (size_t i = 0; i < npages; ++i) {
		pt[i].paddr = flags;
		pt[i].swap_offset = 0xffff;
	}
	return pt;
}

#endif /* OPT-A3 */
