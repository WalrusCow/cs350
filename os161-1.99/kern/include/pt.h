#include <vm.h>
#include <addrspace.h>
#include <segments.h>

// Flags for bits in paddr
#define PT_VALID	0x200
#define PT_WRITE 	0x001
#define PT_READ		0x002
#define PT_EXEC 	0x004

// Structure for data in the page table
struct pte {
	// Page paddr, with flags in last 12 bits
	paddr_t paddr;
};

// Set an entry in the page table
void pt_setEntry(paddr_t paddr, vaddr_t vaddr, seg_type seg, struct addrspace* as);
// Get an entry from the page table
struct pte* get_ptEntry(vaddr_t faultPage, struct addrspace* as);
// Get the page table
struct pte* get_pt(seg_type seg, vaddr_t vaddr, struct addrspace* as);
