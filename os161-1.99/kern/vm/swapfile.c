#include "opt-A3.h"
#if OPT_A3
#include <swapfile.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <synch.h>
#include <uio.h>
#include <vnode.h>

static const char filename[] = "SWAPFILE";

static struct lock* swap_mutex;

static	uint16_t max_pages = 2304; //int16_t
	
static	uint16_t emptyslots; //int16_t
	
static 	struct vnode* swapF;
	
static	bool swaptable[2304];

/*
	takes in the source and destination
	need to later validate pt and tlb, I think this is done after loadpage
	
	called in loadpage?
	loadpage does takes in an as
	
*/
int
swapin_mem(uint16_t file_offset,paddr_t p_dest){
	// load from disk to memory similar to load page
	KASSERT((p_dest&PAGE_FRAME) == p_dest);
	KASSERT(swapF != NULL);
	
	struct iovec iov; // buffer
  	struct uio u;
	
	void* kvaddr = (void*)PADDR_TO_KVADDR(p_dest);
 	uio_kinit(&iov, &u, kvaddr, PAGE_SIZE, file_offset, UIO_READ);
	int result = VOP_READ(swapF,&u);
	
	if(result){
		return result;
	}
	
	lock_acquire(swap_mutex);
	swaptable[file_offset] = true;
	emptyslots++;
	lock_release(swap_mutex);
	
	return 0;
}

/*
	need to invalidate the pt and tlb if it's text segment(inside getppages), read only, we can just ignore
	the above logic should be in getppages?
	
	clean page (zero) is called after getpspages
	
*/
int 
swapout_mem(paddr_t paddr, uint16_t *swap_offset){

	KASSERT((paddr&PAGE_FRAME) == paddr); // should be page index
	
	lock_acquire(swap_mutex);
	
	if(swapF==NULL){
		// create file, lazy initialization
		char* swap_FN = kstrdup(filename);
		int result = vfs_open(swap_FN,O_RDWR|O_CREAT,0,&(swapF));/* Open for read and write */
		if(result){
			lock_release(swap_mutex);
			return result;
		}
	}
	
	if(emptyslots == 0){
		panic("Out of swap space");
		// instead of a semaphore of size == max_pages
	}
	
	struct iovec iov; // buffer
  	struct uio u;
	off_t pageindex;
	
	for(int i = 0; i < max_pages; i++){
		if(swaptable[i]){
			// true, means empty
			pageindex = i;
			swaptable[pageindex]=false; // release the lock, writing does not affect swapinmem
			emptyslots--;
			lock_release(swap_mutex);
			break;
		}
	}
	
	void* kvaddr = (void*)PADDR_TO_KVADDR(paddr);
 	uio_kinit(&iov, &u, kvaddr, PAGE_SIZE, pageindex, UIO_WRITE);
	
	//copy out
	int result = VOP_WRITE(swapF,&u);
	if(result){
		//failed
		lock_acquire(swap_mutex);//acquire lock
		swaptable[pageindex]=true;
		emptyslots++;
		lock_release(swap_mutex);
		// release the locks
		return result;
	}
	//maybe decrement the frame-allocation number
	//the total amount that a process can call "getppages"
	
	*swap_offset = pageindex;
	return 0;
}

/* 	
	free a page in the swap file
*/
void
swap_free(uint16_t swap_offset){

	// do it for page table2 and stack
	lock_acquire(swap_mutex);
	swaptable[swap_offset]=true;
	emptyslots ++;
	lock_release(swap_mutex);

}


void
swap_init(void){
	// initalize swap file table...		
	
	emptyslots = max_pages;
	
	for(int i = 0; i < max_pages; i++){
		swaptable[i] = true;
	}
	
	swap_mutex = lock_create("swap_file_lock");
	
	if(swap_mutex == NULL){
		panic("fail to initialize swap table lock");
	}

}

/*
	when program exits
*/
void
swap_destroy(void){

	if(swapF != NULL){
		vfs_close(swapF);
	} //else the swap file was never in use
	lock_destroy(swap_mutex);
}

#endif
