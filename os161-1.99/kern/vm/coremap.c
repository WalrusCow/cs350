#include <vm.h>
#include <coremap.h>
#include <spinlock.h>

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

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
