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

// system file handler
// NOTE: It is better to actually have rw lock in the vnode
// because then we shouldn't actually even need a global table
struct sysFH {
	struct vnode* vn;
	struct rwlock* rwlock; // readerwriter lock
};

// Global file descriptors
struct sysFH* sysFH_table[__SYS_OPEN_MAX];
// Global lock for file system calls
struct semaphore* file_sem = NULL;

/*
 * Allocate things that must be allocated
 */
void
file_bootstrap(void) {
	// Initialize entries for stdin/stdout/stderr in the system table
	for (int i = 0; i <= 2; ++i) {
		sysFH_table[i] = kmalloc(sizeof(struct sysFH));
		if (sysFH_table[i] == NULL) {
			panic("unable to allocate system file table\n");
		}
		sysFH_table[i]->rwlock = rw_create("console lock");
		sysFH_table[i]->vn = NULL;
	}

	// Initialize global table lock
	file_sem = sem_create("file_sem", 1);
}

/*
 * handler for open() system call
 * `filename` should be a string in user space.
 * See kern/fcntl.h for information on flags.
 */
int
sys_open(char* filename, int flags, int* retval) {
	KASSERT(curproc != NULL); // Some process must be opening the file

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
	int update_global = 1; // initialize to true

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
			update_global = 0; // no need new lock
			break;
		}
	}

	if (fdesc == -1) {
		// System has too many open files - can't find a free fdesc
		V(file_sem);
		vfs_close(openNode);
		return EMFILE;
	}

	int proc_fdesc = -1;
	// Now check that process has free space
	for (int i = 3; i < __OPEN_MAX; ++i) {
		if (curproc->file_arr[i] == NULL) {
			proc_fdesc = i;
			break;
		}
	}

	if (proc_fdesc == -1) {
		// Process has too many open files
		vfs_close(openNode);
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
	new_proc_fh->fd = fdesc; // Store file descriptor, we need the lock

	if(update_global) {
		// Allocate a new reader writer lock for this
		struct rwlock* new_vnode_rw = rw_create("vnode_rw");
		if (new_vnode_rw == NULL) {
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
			rw_destroy(new_vnode_rw);
			return EMFILE;
		}

		// Store the ptr to vnode and the lock for this vnode
		new_sys_fh->vn = openNode;
		new_sys_fh->rwlock = new_vnode_rw;

		sysFH_table[fdesc] = new_sys_fh;
	}

	// Save the results to the process table and the system table
	curproc->file_arr[proc_fdesc] = new_proc_fh;

	// Release the lock
	V(file_sem);

	// Return the file descriptor (relative to the process)
	*retval = proc_fdesc;
	return 0;
}

/*
 * handler for close() system call
 * TODO: Add docs here
 */
int
sys_close(int fd) {

	//vnode pointer
	struct vnode* vn;

	//some process made the system call
	KASSERT(curproc != NULL);

	// Acquire the mutex lock
	P(file_sem);

	// Check valid fd
	if((fd < 3) || (fd >= __OPEN_MAX) || (curproc->file_arr[fd] == NULL) || (curproc->file_arr[fd]->vn == NULL)){
		V(file_sem);
		return EBADF;
	}

	/* TODO: Acquire RW lock for the vnode as "W"
	 * and then continue?  */

	//get the global fd
	int index = curproc->file_arr[fd]->fd;
	//get the lock for this vnode
	rw_wait(sysFH_table[index]->rwlock, (RoW)1);

	//check is it the last process open this file
	int islast = 0;
	if(sysFH_table[index]->vn->vn_opencount == 1){
		islast = 1;
	}

	// Close the vnode
	vn = curproc->file_arr[fd]->vn;
	vfs_close(vn);
	curproc->file_arr[fd]->vn = NULL;

	// Free procFH
	kfree(curproc->file_arr[fd]);
	curproc->file_arr[fd] = NULL;

	//if close the file that only opened by one process
	if(islast){
		if(sysFH_table[index]->rwlock != NULL){
			rw_destroy(sysFH_table[index]->rwlock);
		}

		sysFH_table[index]->vn = NULL;
		kfree(sysFH_table[index]);
		sysFH_table[index] = NULL;
	}

	// Check the system file handler, if no process use this, free it

	/* TODO: This doesn't work -- how to free in the system table?
	 * Idea: Look at the vfs code and determine what the value of the
	 * vnode is that should be 0 (or 1) if nobody else is using this vnode
	 * and then free sysFH_table entry in that case
	 *
	 * As such, it is always false for now.
	 */
/*	if (0 && sysFH_table[index]->vn == NULL) {
		if (sysFH_table[index]->rwlock != NULL) {
			rw_destroy(sysFH_table[index]->rwlock);
		}

		kfree(sysFH_table[index]);
		sysFH_table[index] = NULL;
	}
*/
	V(file_sem);

	// Success
	return 0;
}

/*
 * handler for read() system call
 * TODO: Add docs here
 */
int
sys_read(int fdesc, userptr_t ubuf, unsigned int nbytes, int* retval) {

  struct iovec iov;
  struct uio u;
  int res;

  DEBUG(DB_SYSCALL,"Syscall: read(%d,%x,%d)\n",fdesc,(unsigned int)ubuf,nbytes);

  if ((fdesc<0) || (fdesc >= __OPEN_MAX)||(fdesc==STDOUT_FILENO)||(fdesc==STDERR_FILENO)||(curproc->file_arr[fdesc] == NULL)) {
    return EBADF; // make sure it's not std out/err/or anything not belong to this file
  }
	// Acquire table lock for lookup
  P(file_sem);
  struct procFH* p_fh = curproc->file_arr[fdesc]; // local file lookup

  if (p_fh == NULL) {
	  V(file_sem);
	  return EBADF;
  }

  // System FH for this
  struct sysFH* sys_fh = sysFH_table[p_fh->fd];

  // Acquire the lock for this vnode
  rw_wait(sys_fh->rwlock,(RoW)0); // 0 is reader

  V(file_sem);

  KASSERT(curproc != NULL); // current process
  KASSERT(curproc->file_arr != NULL);
  KASSERT(curproc->p_addrspace != NULL);

  /* set up a uio structure to refer to the user program's buffer (ubuf) */
  // read, from kernel to userspace
  iov.iov_ubase = ubuf;
  iov.iov_len = nbytes;
  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_offset = p_fh->offset;  /* not needed for the console */

  u.uio_resid = nbytes; // initialized to total amount of data
  u.uio_segflg = UIO_USERSPACE; // user process data
  u.uio_rw = UIO_READ; // from kernel to uio_seg
  u.uio_space = curproc->p_addrspace;

  res = VOP_READ(curproc->file_arr[fdesc]->vn,&u);
  if(res){
	rw_signal(sys_fh->rwlock,(RoW)0); // release the lock
	return res;
  }

  *retval = nbytes - u.uio_resid;
  KASSERT(*retval >= 0);

  p_fh->offset += *retval; // update offset
  rw_signal(sys_fh->rwlock,(RoW)0);

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

  DEBUG(DB_SYSCALL,"Syscall: write(%d,%x,%d)\n",fdesc,(unsigned int)ubuf,nbytes);

  if ((fdesc < 0) || (fdesc >= __OPEN_MAX) || (fdesc==STDIN_FILENO)) {
    return EBADF;
  }

  struct iovec iov;
  struct uio u;
  int res;

  // Acquire table lock for lookup
  P(file_sem);
  struct procFH* p_fh = curproc->file_arr[fdesc];
  if (p_fh == NULL) {
	  V(file_sem);
	  return EBADF;
  }

  // System FH for this
  struct sysFH* sys_fh = sysFH_table[p_fh->fd];

  // Acquire the lock for this vnode
  rw_wait(sys_fh->rwlock,(RoW)1);

  V(file_sem);

  KASSERT(curproc != NULL);
  KASSERT(curproc->file_arr != NULL);
  KASSERT(curproc->p_addrspace != NULL);

  /* set up a uio structure to refer to the user program's buffer (ubuf) */
  iov.iov_ubase = ubuf;
  iov.iov_len = nbytes;
  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  // TODO: Does this offset need to be something else?
  // i.e. does it always need to be 0 but we need to call some VOP to
  // shift our posn in the file first?
  u.uio_offset = p_fh->offset; // Set appropriate offset
  u.uio_resid = nbytes;
  u.uio_segflg = UIO_USERSPACE;
  u.uio_rw = UIO_WRITE;
  u.uio_space = curproc->p_addrspace;

  res = VOP_WRITE(curproc->file_arr[fdesc]->vn, &u);

  if (res) {
    rw_signal(sys_fh->rwlock,(RoW)1);
    return res;
  }

  /* pass back the number of bytes actually written */
  int numWritten = nbytes - u.uio_resid;
  *retval = numWritten;
  KASSERT(*retval >= 0);

  // Also increment the offset by how much we wrote
  p_fh->offset += numWritten;
  rw_signal(sys_fh->rwlock,(RoW)1);

  return 0;
}
