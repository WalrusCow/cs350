#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>

#include "opt-A2.h"

#if OPT_A2
#include <synch.h>

// Table of PIDs and what process they belong to
// Note that pidTable[0] == pidTable[1] == NULL, because
// those PIDs cannot be assigned to a user process
// TODO: How to make this visible to proc.c ?
struct proc* pidTable[__PID_MAX + 1] = {NULL};
#endif

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);

  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}

#if OPT_A2
pid_t
sys_getpid(pid_t* retval) {
	*retval = curproc->pid;
	return 0;
}

pid_t
sys_fork(pid_t* retval) {
	// TODO: THIS IS NOT CORRECT: child must not start until
	// this function is done.  maybe we need to copy out
	// a subset of this function
	struct proc* child = proc_create_runprogram(curproc->p_name);

	if (child == NULL) {
		*retval = 0;
		return -1;
	}

	// Copy open files (by reference)
	for (int i = 0; i < __OPEN_MAX; ++i) {
		child->file_arr[i] = curproc->file_arr[i];
	}

	child->parent = curproc;
	*retval = child->pid; // TODO: Different ret val for parent & child
	return 0;
}

pid_t
sys_waitpid(pid_t pid, int* ret, int options) {
	if (ret == NULL) {
		return EFAULT; // Invalid pointer
	}
	if (pid < __PID_MIN || pid > __PID_MAX) {
		return ESRCH; // Invalid PID
	}
	if (options) {
		return EINVAL; // We don't support any options
	}

	// Check if valid
	struct proc* p = pidTable[pid];
	if (p == NULL) {
		return ESRCH; // No process
	}

	if (p->parent != curproc) {
		return ECHILD; // Can only wait on children
	}

	// Check if proc already done (then no need to wait)
	if (p->isDone) {
		*ret = p->exitCode;
		return 0;
	}

	// Tell the child where to store the exit code
	p->codePtr = ret;
	// Now wait on the child's semaphore
	P(p->parentWait);
	// Once we have the semaphore, just release it and return
	// since the return value has already been sent
	V(p->parentWait);
	return 0;
}
#endif /* OPT_A2 */
