
#include "opt-A3.h"

#ifdef UW
/* This was added just to see if we can get things to compile properly and
 * to provide a bit of guidance for assignment 3 */

#include "opt-vm.h"
#if OPT_VM

#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>

#if OPT_A3
#include <kern/errno.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <uw-vmstats.h>
#include <pt.h>

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);
	return addr;
}

static unsigned int next_victim = 0;

void reset_next_victim(void){
	next_victim = 0;
}

static int tlb_get_rr_victim(void){
	int victim;

	victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB;
	return victim;

}
#endif /* OPT-A3 */

void
vm_bootstrap(void)
{
	#if OPT_A3
	/* May need to add code. */
	#endif /* OPT-A3 */
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	#if OPT_A3
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
			return 0;
	}
	return PADDR_TO_KVADDR(pa);
	#else
	/* Adapt code form dumbvm or implement something new */
	(void)npages;
	panic("Not implemented yet.\n");
	return (vaddr_t) NULL;
	#endif /* OPT-A3 */
}

void 
free_kpages(vaddr_t addr)
{
	#if OPT_A3
	/* nothing - leak the memory. */

	(void)addr;
	#else
	/* nothing - leak the memory. */

	(void)addr;
	#endif /* OPT-A3 */
}

void
vm_tlbshootdown_all(void)
{
	panic("Not implemented yet.\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("Not implemented yet.\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{

	#if OPT_A3

	paddr_t paddr;

	struct addrspace *as;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
		case VM_FAULT_READONLY:
			// read-only - error
			return 1;
			/* We always create pages read-write, so we can't get this */
			panic("dumbvm: got VM_FAULT_READONLY\n");
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
		default:
			return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	int segment_type;
	// True if the page is a new one (wasn't in page table)
	bool newPage = false;

	// get the paddr
	int result = pt_getEntry(faultaddress, &paddr, &segment_type);

	if(result) return result;

	bool page_written;

	if((paddr & PT_VALID) == 0){
		newPage = true;
		// Not loaded in page table yet - load it up
		paddr = getppages(1); // new physical page
		as_zero_region(paddr, 1);

		// Only a new text page is not written
		page_written = !(newPage && segment_type == 0);

		// Update page table with this vaddr
		result = pt_setEntry(faultaddress, paddr, page_written);
		// pass in as, since now it does not have to be current address
		// space because load page may take a will -> yield cpu to other
		// process
		if(result) {
			// free page ?
			return result;
		}
	}
	else {
		// Keep track of previous
		page_written = paddr & PT_WRITTEN;

		// Get the actual *address* and ignore the flags from page table
		paddr &= PAGE_FRAME;
	}

	// Must be page aligned
	KASSERT((paddr & PAGE_FRAME) == paddr);

	vmstats_inc(VMSTAT_TLB_FAULT);

	uint32_t tlb_hi, tlb_lo;
	tlb_hi = faultaddress;
	tlb_lo = paddr | TLBLO_VALID;
	if (newPage || segment_type != 0 || !page_written) {
		// Writable if new page or not text segment or not yet written page
		tlb_lo |= TLBLO_DIRTY;
	}

	// Insert into the tlb (choose the index for us)
	int index = tlb_insert(tlb_hi, tlb_lo);

	if (newPage) {
		// Load the page into memory - it is a new page
		result = pt_loadPage(faultaddress, as, segment_type);

		if (result) {
			// Invalidate TLB and page table entries?
			return result;
		}

		// Text segment was writable to load ELF. Fix that.
		if (segment_type == 0) {
			// Update the page table with the new non-written flag
			int spl = splhigh();

			// Remove the written flag from the page table
			// because we have now written the page
			pt_setEntry(faultaddress, paddr, true);

			// Disable interrupts while using TLB
			tlb_lo &= ~TLBLO_DIRTY;
			tlb_write(tlb_hi, tlb_lo, index);
			splx(spl);
			return 0;
		}
	}
	return 0;

	#else
	/* Adapt code from dumbvm or implement something new */
	(void)faulttype;
	(void)faultaddress;
	panic("Not implemented yet.\n");
	return 0;
	#endif /* OPT_A3 */

}

#if OPT_A3
/*
 * tlb_insert just inserts a hi and lo entry into the TLB.
 * It chooses the index appropriately (invalid space if one exists,
 * does replacement otherwise
 */
int
tlb_insert(uint32_t tlb_hi, uint32_t tlb_lo) {
	uint32_t ehi, elo;
	int index;
	// No interrupts while messing with TLB
	int spl = splhigh();

	//if there exists free TLB entry
	for (index = 0; index < NUM_TLB; index++) {
		tlb_read(&ehi, &elo, index);
		if (elo & TLBLO_VALID) {
			continue;
		}
		// Current TLB entry is invalid (free for us to take)
		vmstats_inc(VMSTAT_TLB_FAULT_FREE);

		tlb_write(tlb_hi, tlb_lo, index);
		splx(spl);
		return index;
	}

	// RR replacement in TLB
	vmstats_inc(VMSTAT_TLB_FAULT_REPLACE);
	index = tlb_get_rr_victim();

	tlb_write(tlb_hi, tlb_lo, index);
	splx(spl);
	return index;
}
#endif /* OPT_A3 */

#endif /* OPT_VM */

#endif /* UW */
