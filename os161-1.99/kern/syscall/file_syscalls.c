#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <uio.h>
#include <syscall.h>
#include <vnode.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>

#include "opt-A2.h"

#if OPT_A2

#include <synch.h>
#include <spinlock.h>
#include <kern/limits.h>

// Global lock for file system calls
struct semaphore* file_sem = NULL;
// Spinlock for initializing the semaphore... lol
struct spinlock spinner = { .lk_lock = 0, .lk_holder = NULL };

//system file handler
struct sysFH{
	struct vnode* vn;
	struct semaphore* vn_mutex;
};

struct sysFH* sysFH_table[__SYS_OPEN_MAX];

/*
 * handler for open() system call
 * `filename` should be a string in user space.
 * See kern/fcntl.h for information on flags.
 */
int
sys_open(char* filename, int flags, int* retval) {
	KASSERT(curproc != NULL); // Some process must be opening the file

	spinlock_acquire(&spinner);
	if (file_sem == NULL) {
		// Initialize the semaphore if it's not already... lol
		file_sem = sem_create("file_sem", 1);
	}
	spinlock_release(&spinner);

	// What we are opening
	struct vnode* openNode = NULL;

	if (filename == NULL) {
		return EFAULT; // Invalid pointer
	}

	char* path = kstrdup(filename);
	KASSERT(path); // We must be able to copy this

	// Entering critical section
	P(file_sem);

	// Third argument is `mode` and is currently unused
	int err = vfs_open(path, flags, 0, &openNode);
	kfree(path); // Done with path

	if (err) {
		// Some error from vfs_open
		V(file_sem);
		return err;
	}

	// File descriptor that we will use
	int fdesc = -1;

	/* Try to find a file descriptor that is free, but don't bother
	 * checking the stdin/stdout/stderr numbers (0/1/2) */
	for (int i = 3; i < __SYS_OPEN_MAX; ++i) {
		if (sysFH_table[i] == NULL) {
			// Save first open fdesc if none yet
			if (fdesc == -1) fdesc = i;
		}
		else if (sysFH_table[i]->vn == openNode) {
			// We already have this node then
			fdesc = i;
			break;
		}
	}

	if (fdesc == -1) {
		// System has too many open files - can't find a free fdesc
		V(file_sem);
		return EMFILE;
	}

	int procIndex = -1;
	// Now check that process has free space
	for (int i = 3; i < __OPEN_MAX; ++i) {
		if (curproc->file_arr[i] == NULL) {
			procIndex = i;
			break;
		}
	}

	if (procIndex == -1) {
		// Process has too many open files
		V(file_sem);
		return ENFILE;
	}

	// Allocate the new process file handler
	struct procFH* new_proc_fh = kmalloc(sizeof(struct procFH));
	if (new_proc_fh == NULL) {
		// No memory for process fh
		V(file_sem);
		return EMFILE;
	}
	new_proc_fh->vn = openNode;
	new_proc_fh->offset = 0; // Start at 0 always
	new_proc_fh->fd = fdesc; // Store file descriptor

	// Allocate the new semaphore for this file
	struct semaphore* new_vnode_sem = sem_create("vnode_sem", 1);
	if (new_vnode_sem == NULL) {
		// No memory for semaphore
		V(file_sem);
		kfree(new_proc_fh);
		return EMFILE;
	}

	// Allocate the new system file handler
	struct sysFH* new_sys_fh = kmalloc(sizeof(struct sysFH));
	if (new_sys_fh == NULL) {
		// No memory for system fh
		V(file_sem);
		kfree(new_proc_fh);
		sem_destroy(new_vnode_sem);
		return EMFILE;
	}

	// Store the ptr to vnode and the lock for this vnode
	new_sys_fh->vn = openNode;
	new_sys_fh->vn_mutex = new_vnode_sem;

	// Save the results to the process table and the system table
	curproc->file_arr[procIndex] = new_proc_fh;
	sysFH_table[fdesc] = new_sys_fh;

	// Release the lock
	V(file_sem);

	// Return the file descriptor
	*retval = fdesc;
	return 0;
}

/*
 * handler for close() system call
 * TODO: Add docs here
 */
int
sys_close(int fd) {

        spinlock_acquire(&spinner);
        if (file_sem == NULL) {
                // Initialize the semaphore if it's not already... lol
                file_sem = sem_create("file_sem", 1);
        }
        spinlock_release(&spinner);

	//vnode pointer
	struct vnode* vn;

	//some process made the system call
	KASSERT(curproc != NULL);

	//acquire the mutex lock
	P(file_sem);
	//check valid fd
	if((fd < 3) || (fd >= __OPEN_MAX) || (curproc->file_arr[fd] == NULL) || (curproc->file_arr[fd]->vn == NULL)){
		return EBADF;
	}

	//free the current process file handler
	vn = curproc->file_arr[fd]->vn;
	vfs_close(vn);
	curproc->file_arr[fd]->vn = NULL;

	int index = curproc->file_arr[fd]->fd;

	kfree(curproc->file_arr[fd]);
	curproc->file_arr[fd] = NULL;

	//check the system file handler, if no process use this, free it
	if(sysFH_table[index]->vn == NULL){
		if(sysFH_table[index]->vn_mutex != NULL){
			sem_destroy(sysFH_table[index]->vn_mutex);
		}
	}

	kfree(sysFH_table[index]);
	sysFH_table[index] = NULL;

	V(file_sem);

	//success

	return 0;
}

/*
 * handler for read() system call
 * TODO: Add docs here
 */
int
sys_read(int fdesc, userptr_t ubuf, unsigned int nbytes, int* retval) {

//	spinlock_acquire(&spinner);
// 	if (file_sem == NULL) {
 			// Initialize the semaphore if it's not already... lol
// 			file_sem = sem_create("file_sem", 1);
// 	}
// 	spinlock_release(&spinner);


  
  struct iovec iov;
  struct uio u;
  int res;

  DEBUG(DB_SYSCALL,"Syscall: read(%d,%x,%d)\n",fdesc,(unsigned int)ubuf,nbytes);

  if ((fdesc<0) || (fdesc >= __OPEN_MAX)||(fdesc==STDOUT_FILENO)||(fdesc==STDERR_FILENO)||(curproc->file_arr[fdesc] == NULL)) {
    return EBADF; // make sure it's not std out/err/or anything not belong to this file
  }
//  P(file_sem);
  
  KASSERT(curproc != NULL); // current process
  KASSERT(curproc->file_arr != NULL);
  KASSERT(curproc->p_addrspace != NULL);

  /* set up a uio structure to refer to the user program's buffer (ubuf) */
  // read, from kernal to userspace
  iov.iov_ubase = ubuf;
  iov.iov_len = nbytes;
  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_offset = 0;  /* not needed for the console */
  u.uio_resid = nbytes; // initialized to total amount of data
  u.uio_segflg = UIO_USERSPACE; // user process data
  u.uio_rw = UIO_READ; // from kernel to uio_seg
  u.uio_space = curproc->p_addrspace;

  res = VOP_READ(curproc->file_arr[fdesc]->vn,&u);
  if(res){
	return res;
  }
  *retval = nbytes -u.uio_resid;
  KASSERT(*retval >= 0);
//  V(file_sem);
  
  return res; // error or success;

}

#endif

/* handler for write() system call                  */
/*
 * n.b.
 * This implementation handles only writes to standard output
 * and standard error, both of which go to the console.
 * Also, it does not provide any synchronization, so writes
 * are not atomic.
 *
 * You will need to improve this implementation
 */

int
sys_write(int fdesc, userptr_t ubuf, unsigned int nbytes, int *retval)
{

//	spinlock_acquire(&spinner);
//	if (file_sem == NULL) {
			// Initialize the semaphore if it's not already... lol
//			file_sem = sem_create("file_sem", 1);
//	}
//	spinlock_release(&spinner);

  struct iovec iov;
  struct uio u;
  int res;

  DEBUG(DB_SYSCALL,"Syscall: write(%d,%x,%d)\n",fdesc,(unsigned int)ubuf,nbytes);

  if ((fdesc<0) || (fdesc >= __OPEN_MAX) || (fdesc==STDIN_FILENO) || (curproc->file_arr[fdesc] == NULL)) {
	  //DEBUG(DB_FOO, "BADF\n");
    return EBADF;
  }

  // Acquire lock
//  P(file_sem);

  KASSERT(curproc != NULL);
  KASSERT(curproc->file_arr != NULL);
  KASSERT(curproc->p_addrspace != NULL);

  /* set up a uio structure to refer to the user program's buffer (ubuf) */
  iov.iov_ubase = ubuf;
  iov.iov_len = nbytes;
  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_offset = 0;  /* not needed for the console */
  u.uio_resid = nbytes;
  u.uio_segflg = UIO_USERSPACE;
  u.uio_rw = UIO_WRITE;
  u.uio_space = curproc->p_addrspace;

  res = VOP_WRITE(curproc->file_arr[fdesc]->vn,&u);
  if (res) {
	//DEBUG(DB_FOO, "VOP_WRITE error %x\n", res);
//	V(file_sem);
    return res;
  }

  //DEBUG(DB_FOO, "ret:%d\n", nbytes - u.uio_resid);
  //DEBUG(DB_FOO, "nbytes: %d -- resid: %d\n", nbytes, u.uio_resid);
  /* pass back the number of bytes actually written */
  *retval = nbytes - u.uio_resid;
  KASSERT(*retval >= 0);
//  V(file_sem);
  
  return 0;
}
