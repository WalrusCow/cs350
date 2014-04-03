
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
#include <coremap.h>
#include <segments.h>

//recored is the coremap set up
static bool coremap_set_up = false;

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
	//initialize coremap
	coremaps_init();
	coremap_set_up = true;
	#endif /* OPT-A3 */
}

/* Allocate some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{
	#if OPT_A3
	paddr_t pa;
	//before coremap set up
	if(coremap_set_up == false){
		pa = getppages(npages);
	}
	//after coremap set up
	else{
		//for kernel, no address space
		pa = coremaps_getppages(npages, NULL, 0);
	}
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
	// free page
	coremaps_free(KVADDR_TO_PADDR(addr),0xffff);
	// the swapoffset is not used for kernel
	#else
	/* nothing - leak the memory. */

	(void)addr;
	#endif /* OPT-A3 */
}

void
vm_tlbshootdown_all(void)
{
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (int i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	int spl = splhigh();
	uint32_t hi = ts->ts_vaddr;
	int index = tlb_probe(hi,0);
	if(index >= 0){
		tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
	}
	splx(spl);
	// else discard
	
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{

	#if OPT_A3

	paddr_t paddr;
	uint16_t swap_offset;
	struct pte PTE;
	struct addrspace *as;

	faultaddress &= PAGE_FRAME;

	switch (faulttype) {
		case VM_FAULT_READONLY:
			// read-only - error
			return 1;
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
		default:
			return EINVAL;
	}

	if (curproc == NULL) {
		// Probably kernel fault
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		// Probably kernel fault
		return EFAULT;
	}

	seg_type segment_type;
	int result = get_seg_type(faultaddress, as, &segment_type);
	if (result) return result;

	// True if the page is a new one (wasn't in page table)
	bool newPage = false;

	// get the paddr
	result = pt_getEntry(faultaddress, &PTE);
	paddr = PTE.paddr;
	swap_offset = PTE.swap_offset;

	if((paddr & PT_VALID) == 0){
		newPage = true;
		// Not loaded in page table yet - load it up
		paddr = coremaps_getppages(1, as, faultaddress); // new physical page

		// Update page table with this vaddr
		result = pt_setEntry(faultaddress, paddr);
		// pass in as, since now it does not have to be current address
		// space because load page may take a will -> yield cpu to other
		// process
		if (result) {
			// free page ? probably...
			return result;
		}
	}
	else {
		vmstats_inc(VMSTAT_TLB_RELOAD);
		// Get the actual *address* and ignore the flags from page table
		paddr &= PAGE_FRAME;
	}

	// Must be page aligned
	KASSERT((paddr & PAGE_FRAME) == paddr);

	vmstats_inc(VMSTAT_TLB_FAULT);

	uint32_t tlb_hi, tlb_lo;
	tlb_hi = faultaddress;
	tlb_lo = paddr | TLBLO_VALID;
	// Text segment is not writeable
	if (segment_type != TEXT) tlb_lo |= TLBLO_DIRTY;

	// Insert into the tlb (choose the index for us)
	tlb_insert(tlb_hi, tlb_lo);

	if (newPage) {
		// Load the page into memory - it is a new page
		result = pt_loadPage(faultaddress, paddr, swap_offset, as, segment_type);

		if (result) {
			// Invalidate TLB and page table entries?
			return result;
		}
		// TODO: Update TLB and page table here, or above?
		// ... it might be better to do it here.
		// one thing to think about: can we swap out the physical page
		// in between then and now?  I seriously doubt it.
		// Maybe put in a check when choosing a page to swap to check if
		// the page has been loaded, and only swap those that have?
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
