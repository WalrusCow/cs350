
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

	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

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
	bool notInPt = true; // True if vaddr not in page table

	// get the paddr
	int result = pt_getEntry(faultaddress, &paddr, &segment_type);

	if(result) return result;

	if((paddr & PT_VALID) == 0){
		// Not loaded in page table yet - load it up
		paddr = getppages(1); // new physical page
		as_zero_region(paddr, 1); // Zero the page, because that's good

		// Update page table with this vaddr
		result = pt_setEntry(faultaddress, paddr);
		// pass in as, since now it does not have to be current address
		// space because load page may take a will -> yield cpu to other
		// process
		if(result) {
			// free page ?
			return result;
		}
		// We just updated the page table to have this vaddr
		notInPt = false;

		/* make sure it's page-aligned */
		KASSERT((paddr & PAGE_FRAME) == paddr);
	}
	else {
		// Get the actual *address* and ignore the flags from page table
		paddr &= PAGE_FRAME;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	vmstats_inc(VMSTAT_TLB_FAULT);

	int index = 0;

	//if there exists free TLB entry
	for (; index < NUM_TLB; index++) {
		tlb_read(&ehi, &elo, index);
		if (elo & TLBLO_VALID) {
			continue;
		}
		// Current TLB entry is invalid (free for us to take)
		vmstats_inc(VMSTAT_TLB_FAULT_FREE);
		ehi = faultaddress;

		if (!notInPt && segment_type == 0) {
			// If we found in the page table and it's text segment then
			// it's not writable (we have already written)
			elo = paddr | TLBLO_VALID;
		}
		else {
			elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		}

		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, index);
		splx(spl);
		break;
	}

	// We went through the whole TLB without finding the vaddr
	if(index == NUM_TLB){
		// RR replacement in TLB
		vmstats_inc(VMSTAT_TLB_FAULT_REPLACE);
		index = tlb_get_rr_victim();
		ehi = faultaddress;

		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, index);
		splx(spl);
	}

	if(notInPt){
		// Load the page into memory
		result = pt_loadPage(faultaddress, as, segment_type);

		if(result){
			// free ?
			return result;
		}

		// Text segment was writable to load ELF. Fix that.
		if(segment_type == 0){
			elo &= ~TLBLO_DIRTY;
			tlb_write(ehi, elo, index);
		}
	}
	return 0;

	#else
	/* Adapt code from dumbvm or implement something new */
	(void)faulttype;
	(void)faultaddress;
	panic("Not implemented yet.\n");
	return 0;
	#endif /* OPT-A3 */

}
#endif /* OPT_VM */

#endif /* UW */
