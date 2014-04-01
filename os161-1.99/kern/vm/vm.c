
#ifdef UW
/* This was added just to see if we can get things to compile properly and
 * to provide a bit of guidance for assignment 3 */

#include "opt-vm.h"
#if OPT_VM

#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <segment.h>

#include <spl.h>
#include <spinlock.h>
#include <kern/errno.h>
#include <mips/tlb.h>
#include <current.h>
#include <proc.h>
#include <uw-vmstats.h>
#include <coremap.h>

// TODO: Use the bootstrap function?
static struct spinlock victim_lock = SPINLOCK_INITIALIZER;
static volatile unsigned int next_victim = 0;

void
vm_bootstrap(void) {
	/* May need to add code. */
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages) {
	// TODO: this is dumbvm code
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr) {
	/* nothing - leak the memory. */

	(void)addr;
}

static int
get_next_victim(void) {
	spinlock_acquire(&victim_lock);
	int victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB;
	spinlock_release(&victim_lock);
	return victim;
}

/*
 * Invalidate whole TLB
 */
void
vm_tlbshootdown_all(void) {
	// No interrupts while using TLB
	int spl = splhigh();
	for (int i = 0; i < NUM_TLB; ++i) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	next_victim = 0;

	splx(spl);

	vmstats_inc(VMSTAT_TLB_INVALIDATE);
}

void
vm_tlbshootdown(const struct tlbshootdown *ts) {
	(void)ts;
	// TODO: Do we need this function?
	vm_tlbshootdown_all();
}

int
vm_fault(int faulttype, vaddr_t faultPage) {
	// faultaddress not in TLB so check the PT
	// If not in PT, request a new page and update PT & TLB

	// We only care about the page
	faultPage &= PAGE_FRAME;

	switch (faulttype) {
		case VM_FAULT_READONLY:
			// Error
			return 1;
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
		default:
			return EINVAL;
	}

	// Probably kernel fault in boot
	if (curproc == NULL) return EFAULT;

	struct addrspace* as = curproc_getas();
	// Probably kernel fault in boot
	if (as == NULL) return EFAULT;

	paddr_t paddr;

	seg_type seg;
	int err = get_seg_type(faultPage, as, &seg);
	if (err) return err;

	// Get the page table entry
	struct pte* pte = get_ptEntry(faultPage, seg, as);
	// Invalid address, somehow
	if (pte == NULL) return EFAULT;

	if (pte->paddr & PT_VALID) {
		// Entry is valid
		// Discard the flags
		paddr = pte->paddr & PAGE_FRAME;
	}
	else {
		// Entry not valid
		// Now we must request a new page and update the page table and TLB
		paddr = getppages(1);

		// Now also prepare the page, as necessary
		prepare_page(paddr, faultPage, seg, as);

		// Now update the page table
		pt_setEntry(paddr, faultPage, seg, as);
	}

	// Disable interrupts while using TLB
	int spl = splhigh();

	uint32_t tlb_hi = faultPage;
	// TODO: dirty bit off for text segment
	uint32_t tlb_lo = paddr | TLBLO_VALID | TLBLO_DIRTY;

	// Insert into the TLB
	tlb_insert(tlb_hi, tlb_lo);

	splx(spl);
	return 0;
}

/*
 * Insert to the TLB. Replaces if necessary.
 * Interrupts should be switched off prior to calling.
 * This returns the index of the replaced element.
 */
int
tlb_insert(uint32_t tlb_hi, uint32_t tlb_lo) {
	// Search for a valid entry
	int idx = 0;
	// Used for reading from TLB
	uint32_t ehi, elo;
	for (idx = 0; idx < NUM_TLB; idx++) {
		tlb_read(&ehi, &elo, idx);
		// Don't replace a valid entry
		if (elo & TLBLO_VALID) continue;
		tlb_write(tlb_hi, tlb_lo, idx);
		return idx;
	}

	idx = get_next_victim();
	tlb_write(tlb_hi, tlb_lo, idx);
	return idx;
}
#endif /* OPT_VM */

#endif /* UW */
