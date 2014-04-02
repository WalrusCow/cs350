/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt-A3.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#ifdef UW
#include <proc.h>
#endif

#if OPT_A3
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <uw-vmstats.h>
#include <pt.h>
#include <vfs.h>
#include <coremap.h>

void
as_zero_region(paddr_t paddr, unsigned npages)
{
        bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}


#endif /* OPT-A3 */
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{

	#if OPT_A3

	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	// Segments
	as->text_seg = NULL;
	as->data_seg = NULL;
	as->stack_seg = NULL;

	//page table
	as->text_pt = NULL;
	as->data_pt = NULL;
	as->stack_pt = NULL;

	//vnode
	as->as_vn = NULL;

	return as;

	#else
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	return as;
	#endif /* OPT-A3 */
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	#if OPT_A3

	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	// TODO

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	//copy the vnode and page table in the address space
	// TODO: increment references
	new->as_vn = old->as_vn;
	//new->text_pt = kmalloc(sizeof(paddr_t) * new->as_npages1);
	//new->data_pt = kmalloc(sizeof(paddr_t) * new->as_npages2);
	//new->stack_pt = NULL; // TODO: Do we kmalloc here?

	//Do the deep copy? what about stack? use memmove?

	*ret = new;
	return 0;

	#else
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	(void)old;
	
	*ret = newas;
	return 0;
	#endif /* OPT-A3 */
}

void
as_destroy(struct addrspace *as)
{

	#if OPT_A3
	// Close the vnode (this was opened at runtime by runprogram)
	vfs_close(as->as_vn);

	//free all used physical memory
	coremaps_as_free(as);

	// Paranoia: Check all sub-elements
	if (as->text_seg != NULL) kfree(as->text_seg);
	if (as->data_seg != NULL) kfree(as->data_seg);
	if (as->stack_seg != NULL) kfree(as->stack_seg);

	if (as->text_pt != NULL) kfree(as->text_pt);
	if (as->data_pt != NULL) kfree(as->data_pt);
	if (as->stack_pt != NULL) kfree(as->stack_pt);
	kfree(as);

	#else
	kfree(as);
	#endif /* OPT-A3 */
}

void
as_activate(void)
{

	#if OPT_A3

	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
	/* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
			return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	vmstats_inc(VMSTAT_TLB_INVALIDATE);
	reset_next_victim();

	splx(spl);

	#else
	struct addrspace *as;

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	#endif /* OPT-A3 */
}

void
#ifdef UW
as_deactivate(void)
#else
as_dectivate(void)
#endif
{
	#if OPT_A3
	#endif /* OPT-A3 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{

	#if OPT_A3
	// Set up the flags that we will be using
	int flags = 0;
	if (readable) flags |= PT_READ;
	if (writeable) flags |= PT_WRITE;
	if (executable) flags |= PT_EXE;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	size_t npages = sz / PAGE_SIZE;

	if (as->text_pt == NULL) {
		// Space for page table
		as->text_pt = kmalloc(sizeof(paddr_t) * npages);
		if (as->text_pt == NULL) return ENOMEM;

		// Initialize the page table
		for(size_t i = 0; i < npages; i++){
			as->text_pt[i] = flags;
		}
		return 0;
	}

	if (as->data_pt == NULL) {
		// Space for page table
		as->data_pt = kmalloc(sizeof(paddr_t) * npages);
		if (as->text_pt == NULL) return ENOMEM;

		// Initialize the page table
		for(size_t i = 0; i < npages; i++){
			as->data_pt[i] = flags;
		}
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;

	#else
	(void)as;
	(void)vaddr;
	(void)sz;
	(void)readable;
	(void)writeable;
	(void)executable;
	return EUNIMP;
	#endif /* OPT-A3 */
}

int
as_prepare_load(struct addrspace *as)
{
	#if OPT_A3

	(void) as;
	/*
	 * Write this.
	 */
	return 0;

	#else
	(void)as;
	return 0;
	#endif /* OPT-A3 */
}

int
as_complete_load(struct addrspace *as)
{

	#if OPT_A3
	/*
	 * Write this.
	 */

	(void)as;
	return 0;

	#else
	(void)as;
	return 0;
	#endif /* OPT-A3 */
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	#if OPT_A3
	as->stack_pt = kmalloc(VM_STACKPAGES * sizeof(paddr_t));
	if (as->stack_pt == NULL) return ENOMEM;

	as->stack_seg = kmalloc(sizeof(struct segment));
	if (as->stack_seg == NULL) return ENOMEM;

	// Initialize a special "segment". Note that this isn't really
	// a segment, because it doesn't live in the ELF file.
	// However, treating it as such simplifies a bunch of code nicely,
	// so we will do that.
	as->stack_seg->type = STACK;
	as->stack_seg->filesize = 0;
	as->stack_seg->file_offset = 0;
	as->stack_seg->vtop = USERSTACK;
	as->stack_seg->vbase = STACK_BASE;
	as->stack_seg->npages = VM_STACKPAGES;

	*stackptr = USERSTACK;
	return 0;

	#else
	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
	#endif
}

