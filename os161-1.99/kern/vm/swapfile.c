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

static	int16_t max_pages = 2304; //int16_t
	
static	int16_t emptyslots; //int16_t
	
static 	struct vnode * swapF;
	
static	bool swaptable[2304];

/*
	takes in the source and destination
	need to later validate pt and tlb, I think this is done after loadpage
	
	called in loadpage?
	loadpage does takes in an as
	
*/
int
swapin_mem(int16_t file_offset,paddr_t p_dest){
	// load from disk to memory similar to load page
	KASSERT((p_dest&PAGE_FRAME) == p_dest);
	KASSERT(swapF != NULL);
	KASSERT(file_offset >= 0 && (file_offset < max_pages));
	
	
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
swapout_mem(paddr_t paddr, int16_t *swap_offset){

	KASSERT((paddr&PAGE_FRAME) == paddr); // should be page index
	
	lock_acquire(swap_mutex);
	
	if(swapF==NULL){
		// create file, lazy initialization
		char* swap_FN;
		strcpy(swap_FN, filename);
		int result = vfs_open(swap_FN,O_RDWR,0,&(swapF));/* Open for read and write */
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
	when exit, should set all pages in swap file to be free
	called in as_destroy
	
	for each PTE, if not valid and has a swap entry
	get index and free
	
	return # of empty slots generated from this call
*/
int
swapfree(struct addrspace *as){
	int freed_slots = 0;
	
	KASSERT(as!=NULL);
	KASSERT(as->text_seg!=NULL);
	KASSERT(as->stack_seg!=NULL);
	
	// do it for page table2 and stack
	for(int i = 0; i < as->text_seg->npages ; i++){ // segment.c data size
		// if it's not valid -> not in p memory
		// and it is loaded
		struct pte PTE = as->data_pt[i];
		if(!(PTE->paddr & PT_VALID)&&PTE->swap_offset!=0xffff){
			lock_acquire(swap_mutex);
			swaptable[PTE->swap_offset]=true;
			freed_slots ++;
			emptyslots ++;
			lock_release(swap_mutex);
		}
	}
	
	for(int i = 0; i < as->stack_seg->npages ; i++){ // segment.c stack size
		struct pte PTE = as->stack_pt[i];
		if(!(PTE->paddr & PT_VALID)&&PTE->swap_offset!=0xffff){
			lock_acquire(swap_mutex);
			swaptable[PTE->swap_offset]=true;
			freed_slots ++;
			emptyslots ++;
			lock_release(swap_mutex);
		}
	}
	
	return freed_slots;
}


/*
	-1 means fail,
*/
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
