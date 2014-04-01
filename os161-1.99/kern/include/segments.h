#include <types.h>
#include <vm.h>
#include <addrspace.h>

// enum for types of segments
typedef enum { TEXT, DATA, STACK } seg_type;

// Structure for the relevant data from an elf file segment
struct segment {
	off_t file_offset;
	size_t length;
	vaddr_t vbase;
	vaddr_t vtop;
	seg_type type;
};

// Update the addrspace with the appropriate segments
void make_segment(struct addrspace* as);
