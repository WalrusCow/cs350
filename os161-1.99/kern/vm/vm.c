
#ifdef UW
/* This was added just to see if we can get things to compile properly and
 * to provide a bit of guidance for assignment 3 */

#include "opt-vm.h"
#if OPT_VM

#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>

#include <spl.h>
#include <spinlock.h>
#include <kern/errno.h>
#include <mips/tlb.h>
#include <current.h>
#include <proc.h>
#include <uw-vmstats.h>

// TODO: Use the bootstrap function?
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock victim_lock = SPINLOCK_INITIALIZER;
static volatile unsigned int next_victim = 0;

void
vm_bootstrap(void) {
	/* May need to add code. */
}

paddr_t
getppages(unsigned long npages) {
	// TODO: This is dumbvm code
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);
	// TODO: This obviously needs to change
	addr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);

	return addr;
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
vm_fault(int faulttype, vaddr_t faultaddress) {
	faultaddress &= PAGE_FRAME;

	switch (faulttype) {
		case VM_FAULT_READONLY:
			// Error
			return 1;
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
		default:
			return EINVAL; }

	// Probably kernel fault in boot
	if (curproc == NULL) return EFAULT;

	struct addrspace* as = curproc_getas();
	// Probably kernel fault in boot
	if (as == NULL) return EFAULT;

	paddr_t paddr;
	seg_type seg;
	// Convert the virtual address into a physical one. Also get segment type.
	int err = read_vaddr(faultaddress, as, &paddr, &seg);
	if (err) return err;

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	uint32_t tlb_hi = faultaddress;
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

int
read_vaddr(vaddr_t vaddr, struct addrspace* as, paddr_t* paddr, seg_type* seg) {
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - VM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (vaddr >= vbase1 && vaddr < vtop1) {
		*paddr = (vaddr - vbase1) + as->as_pbase1;
		*seg = TEXT;
	}
	else if (vaddr >= vbase2 && vaddr < vtop2) {
		*paddr = (vaddr - vbase2) + as->as_pbase2;
		*seg = DATA;
	}
	else if (vaddr >= stackbase && vaddr < stacktop) {
		*paddr = (vaddr - stackbase) + as->as_stackpbase;
		*seg = STACK;
	}
	else {
		return EFAULT;
	}
	return 0;
}

#endif /* UW */
