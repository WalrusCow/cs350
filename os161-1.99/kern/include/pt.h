#include "opt-A3.h"
#if OPT_A3

#define PT_VALID 0x00000200
#define PT_DIRTY 0x00000400

#define PT_READ 0X00000080
#define PT_WRITE 0x00000040
#define PT_EXE 0x00000020

int pt_getEntry(vaddr_t vaddr, paddr_t* paddr, int* segment_type);

int pt_setEntry(vaddr_t vaddr, paddr_t paddr);

int pt_loadPage(vaddr_t vaddr, paddr_t* paddr, struct addrspace *as, int segment_type);

#endif /* OPT-A3 */
