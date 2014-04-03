#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include <addrspace.h>
#include <vm.h>
#include <types.h>
// Possible types of segments / virtual addresses
typedef enum { TEXT, DATA, STACK } seg_type;

struct segment {
	off_t file_offset;
	size_t filesize;
	vaddr_t vbase;
	vaddr_t vtop;
	seg_type type;
	unsigned int npages;
};

// Get the type of the vaddr, return error if invalid vaddr
int get_seg_type(vaddr_t vaddr, struct addrspace* as, seg_type* seg);

// Get the corresponding segment from the address space
struct segment* get_segment(seg_type type, struct addrspace* as);

// Easy segment creation
struct segment* seg_create(seg_type type, off_t offset, size_t filesz,
		size_t memsz, vaddr_t vbase);

#endif /* _SEGMENTS_H_ */
