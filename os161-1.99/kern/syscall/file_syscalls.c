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
#include <kern/limits.h>

// Global lock for file system calls
struct semaphore* file_sem = NULL;

/*
 * handler for open() system call
 * `filename` should be a string in user space.
 * See kern/fcntl.h for information on flags.
 */
int
sys_open(userptr_t filename, int flags) {
	if (file_sem == NULL) {
		file_sem = sem_create("file_sem", 1);
	}

	// Default to no error
	int err = 0;

	if (filename == NULL) {
		// Invalid pointer
		err = EFAULT;
		goto done;
	}

	// We need to copy filename into our memory
	P(file_sem);

	KASSERT(curproc != NULL); // Some process must be opening the file
	// File descriptor that we will use
	int fdesc = -1;

	/* Try to find a file descriptor that is free, but don't bother
	 * checking the stdin/stdout/stderr numbers (0/1/2) */
	for (int i = 3; i < __OPEN_MAX; ++i) {
		if (curproc->fh_arr[i] == NULL) {
			// This is the one for us
			fdesc = i;
			break;
		}
	}

done:
	V(file_sem);
	return err;
}

/*
 * handler for close() system call
 * TODO: Add docs here
 */
int
sys_close(int a/* TODO */) {
	(void)a;
	return -1;
}

/*
 * handler for read() system call
 * TODO: Add docs here
 */
int
sys_read(int a/* TODO */) {
	(void)a;
	return -1;
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

  /* only stdout and stderr writes are currently implemented */
  if (!((fdesc==STDOUT_FILENO)||(fdesc==STDERR_FILENO))) {
    return EUNIMP;
  }
  KASSERT(curproc != NULL);
  KASSERT(curproc->file_arr[0] != NULL);
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

  res = VOP_WRITE(curproc->file_arr[0],&u);
  if (res) {
    return res;
  }

  /* pass back the number of bytes actually written */
  *retval = nbytes - u.uio_resid;
  KASSERT(*retval >= 0);
  return 0;
}
