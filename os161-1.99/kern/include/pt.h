#ifndef _PT_H_
#define _PT_H_

#include "opt-A3.h"

#if OPT_A3
#include <segments.h>
#include <vm.h>

#define PT_VALID 0x00000200
#define PT_DIRTY 0x00000400

#define PT_READ 0X00000080
#define PT_WRITE 0x00000040
#define PT_EXE 0x00000020

int
pt_getEntry(vaddr_t vaddr, paddr_t* paddr);

int
pt_setEntry(vaddr_t vaddr, paddr_t paddr);

int
pt_loadPage(vaddr_t vaddr, paddr_t paddr, struct addrspace *as, seg_type type);

paddr_t*
get_pt(seg_type type, struct addrspace* as);

#endif /* OPT-A3 */

#endif /* _PT_H_ */
