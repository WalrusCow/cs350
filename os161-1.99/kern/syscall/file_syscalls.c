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

/*
 * handler for open() system call
 * `filename` should be a string in user space.
 * See kern/fcntl.h for information on flags.
 */
int
sys_open(char* filename, int flags) {
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
		return EFAULT; // Invalid pointer;
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
	for (int i = 3; i < __OPEN_MAX; ++i) {
		struct vnode* curNode = curproc->file_arr[i];
		if (curNode == openNode) {
			// We already have this node then
			fdesc = i;
			break;
		}
		else if (curNode == NULL && fdesc == -1) {
			// Save the first available fdesc in case we don't
			// find that we already have opened the file
			// (this is usually the case)
			fdesc = i;
		}
	}

	if (fdesc == -1) {
		// Process has too many open files - can't find a free fdesc
		V(file_sem);
		return EMFILE;
	}

	V(file_sem);
	return fdesc;
}

/*
 * handler for close() system call
 * TODO: Add docs here
 */
int
sys_close(int fd, int *retval) {

	struct vnode* vn;

	KASSERT(curproc != NULL);

	if((fd < 0) || (fd >= __OPEN_MAX) || (curproc->file_arr[fd] == NULL)){
		return EBADF;
	}

	vn = curproc->file_arr[fd];
	vfs_close(vn);
	curproc->file_arr[fd] = NULL;

	//success
	*retval = 0;

	return 0;
}

/*
 * handler for read() system call
 * TODO: Add docs here
 */
int
sys_read(int fdesc, userptr_t ubuf, unsigned int nbytes) {
  
  struct iovec iov;
  struct uio u;
  int res;

  DEBUG(DB_SYSCALL,"Syscall: read(%d,%x,%d)\n",fdesc,(unsigned int)ubuf,nbytes);

  if ((fdesc<0) || (fdesc > __OPEN_MAX)||(fdesc==STDOUT_FILENO)||(fdesc==STDERR_FILENO)||(curproc->file_arr[fdesc] == NULL)) {
    return EBADF; // make sure it's not std out/err/or anything not belong to this file
  }
  
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

  res = VOP_READ(curproc->file_arr[fdesc],&u);
  
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
sys_write(int fdesc,userptr_t ubuf,unsigned int nbytes,int *retval)
{
  struct iovec iov;
  struct uio u;
  int res;

  DEBUG(DB_SYSCALL,"Syscall: write(%d,%x,%d)\n",fdesc,(unsigned int)ubuf,nbytes);

  if ((fdesc<0) || (fdesc > __OPEN_MAX)||(fdesc==STDIN_FILENO)||(curproc->file_arr[fdesc] == NULL)) {
    return EBADF;
  }
  
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

  res = VOP_WRITE(curproc->file_arr[fdesc],&u);
  if (res) {
    return res;
  }

  /* pass back the number of bytes actually written */
  *retval = nbytes - u.uio_resid;
  KASSERT(*retval >= 0);
  return 0;
}
