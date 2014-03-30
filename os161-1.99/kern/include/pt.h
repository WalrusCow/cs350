#include "opt-A3.h"
#if OPT_A3

#define PT_VALID 0x00000200
#define PT_DIRTY 0x00000400

// Was this page written properly yet?
#define PT_WRITTEN 0x00000001

#define PT_READ 0X00000080
#define PT_WRITE 0x00000040
#define PT_EXE 0x00000020

int
pt_getEntry(vaddr_t vaddr, paddr_t* paddr, int* segment_type);

int
pt_setEntry(vaddr_t vaddr, paddr_t paddr, bool written);

int
pt_loadPage(vaddr_t vaddr, struct addrspace *as, int segment_type);

paddr_t*
pt_getTable(vaddr_t vaddr, struct addrspace* as, int* segType, vaddr_t* vbase);

#endif /* OPT-A3 */
