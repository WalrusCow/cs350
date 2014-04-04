#include "opt-A3.h"
#if OPT_A3
#include <swapfile.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <synch.h>
#include <uio.h>
#include <vnode.h>

static struct lock* swap_mutex;

static uint16_t max_pages = SWAPFILE_PAGES;

static struct vnode* swap_vn;

// Max 9 MB swap file
static bool swaptable[SWAPFILE_PAGES];

/*
	takes in the source and destination
	need to later validate pt and tlb, I think this is done after loadpage

	called in loadpage?
	loadpage does takes in an as
*/
int
swapin_mem(uint16_t pageIndex, paddr_t p_dest){
	// load from disk to memory similar to load page
	KASSERT((p_dest&PAGE_FRAME) == p_dest);
	KASSERT(swap_vn != NULL);

	struct iovec iov; // buffer
	struct uio u;

	off_t file_offset = pageIndex * PAGE_SIZE;

	void* kvaddr = (void*)PADDR_TO_KVADDR(p_dest);
	uio_kinit(&iov, &u, kvaddr, PAGE_SIZE, file_offset, UIO_READ);
	int result = VOP_READ(swap_vn, &u);

	if (result) {
		return result;
	}

	swap_free(pageIndex);
	return 0;
}

/*
	need to invalidate the pt and tlb if it's text segment(inside getppages),
	read only, we can just ignore the above logic should be in getppages?

	clean page (zero) is called after getppages
*/
int
swapout_mem(paddr_t paddr, uint16_t *swap_page){

	KASSERT((paddr & PAGE_FRAME) == paddr); // should be page index

	lock_acquire(swap_mutex);

	struct iovec iov; // buffer
	struct uio u;
	int pageIndex = -1;

	for(int i = 0; i < max_pages; i++){
		// Skip filled pages
		if (!swaptable[i]) continue;

		// Allocate page
		pageIndex = i;
		swaptable[pageIndex] = false;
		break;
	}

	// We don't need the lock for VOP_WRITE
	lock_release(swap_mutex);

	// We didn't find a free page - panic
	if (pageIndex == -1) panic("Out of swap space!\n");

	off_t file_offset = pageIndex * PAGE_SIZE;
	void* kvaddr = (void*)PADDR_TO_KVADDR(paddr);
	uio_kinit(&iov, &u, kvaddr, PAGE_SIZE, file_offset, UIO_WRITE);

	// Write the page to the swap file
	int result = VOP_WRITE(swap_vn, &u);
	if (result) {
		panic("Swap write failed!\n");
	}

	// maybe decrement the frame-allocation number
	// the total amount that a process can call "getppages"

	*swap_page = pageIndex;
	return 0;
}

/*
	free a page in the swap file
*/
void
swap_free(uint16_t pageIndex){

	KASSERT(pageIndex < SWAPFILE_PAGES);

	// do it for page table2 and stack
	lock_acquire(swap_mutex);
	swaptable[pageIndex] = true;
	lock_release(swap_mutex);
}


/*
 * Initialize the swap file. Panic if can't.
 */
void
swap_init(void){
	// initalize swap file table...

	for(int i = 0; i < max_pages; i++){
		swaptable[i] = true;
	}

	swap_mutex = lock_create("swap_file_lock");

	if(swap_mutex == NULL){
		panic("fail to initialize swap table lock\n");
	}

	// create file, lazy initialization
	char* filename = kstrdup(SWAPFILE_NAME);
	if (filename == NULL) panic("failed to copy swapfile name\n");

	// Open the file
	int result = vfs_open(filename, O_RDWR | O_CREAT, 0, &swap_vn);
	if (result) {
		panic("failed to open swap file\n");
	}
}

/*
	when program exits
*/
void
swap_destroy(void){
	vfs_close(swap_vn);
	// else the swap file was never in use
	lock_destroy(swap_mutex);
}

#endif
