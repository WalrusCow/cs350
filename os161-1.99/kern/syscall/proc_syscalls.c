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
#include <vnode.h>
#include <clock.h>
#include <synch.h>
#include <machine/trapframe.h>
#include <limits.h>
#include <copyinout.h>
#include <test.h>

#include <kern/wait.h>

#define PROC_DESTROY_TIME 2

void entry(void* data1, unsigned long data2);

#endif /* OPT_A2 */

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode, int flag) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */

#if OPT_A2
#else
  (void)exitcode;
#endif /* OPT-A2 */

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

#if OPT_A2
	//current process should not be NULL
	KASSERT(curproc != NULL);

	// update the fields of the current process
	// Use the appropriate macro for assigning the exit code
	if (flag == _EXIT_CALLED) {
		curproc->exitCode = _MKWAIT_EXIT(exitcode);
	}
	else {
		curproc->exitCode = _MKWAIT_SIG(exitcode);
	}
	curproc->isDone = true;

	//signal the wait
	V(p->parentWait);

#endif /* OPT-A2 */

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
#if OPT_A2
  // lol, we actually want to wait for some time first
  clocksleep(PROC_DESTROY_TIME);
#endif /* OPT_A2 */
  proc_destroy(p);

  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}

#if OPT_A2
int
sys_getpid(pid_t* retval) {
	*retval = curproc->pid;
	return 0;
}

void
entry(void* data1, unsigned long data2){
	// 1st function in called in child process
	// manipulate trapframe,
	// next instruction, see syscall.c
	(void)data2;
	struct trapframe* tf = (struct trapframe*) data1;
	tf->tf_epc += 4;
	tf->tf_v0 = 0; //return pid = 0
	tf->tf_a3 = 0; // no error

	enter_forked_process(tf);
}

int
sys_fork(pid_t* retval,struct trapframe *tf) {
	struct proc* child = proc_create_runprogram(curproc->p_name);
	// copy the process, has zero thread, 1 thread can not belong to 1+ process
	// need to create main thread....

	if (child == NULL) {
		// Not enough memory
		*retval = 0;
		return ENOMEM;
	}
	else if (child->pid == -1) {
		// No pid was available - too many processes
		proc_destroy(child);
		return ENPROC;
	}

	// copy address space
	int result = as_copy(curproc->p_addrspace,&(child->p_addrspace));
	if(result){
		// fail to copy address space
		proc_destroy(child);
		return result;
	}
	//make a copy of tf in kernal space kmalloc
	//parent need the original for return value
	//copy

	struct trapframe* new_tf = kmalloc(sizeof(struct trapframe));
	if(new_tf == NULL){
		proc_destroy(child);
		return ENOMEM;
	}
	memcpy(new_tf,tf,sizeof(struct trapframe)); // trapframesize

	// need to increment counters
	// 0 1 2 are initialized in proc_create
	// same process may not change file_arr using different thread, inconsistency
	// acquire the global lock
	for (int i = 3; i < __OPEN_MAX; ++i) {
		if(curproc->file_arr[i]!=NULL){
			//full copy
			child->file_arr[i] = kmalloc(sizeof(struct procFH));
			if(child->file_arr[i] == NULL){
				kfree(new_tf);
				proc_destroy(child);
				return ENOMEM;
			}
			memcpy(child->file_arr[i],curproc->file_arr[i],sizeof(struct procFH));
			// TODO: Add refcount to sysFH table & w.e
			VOP_INCREF(child->file_arr[i]->vn);
		}
	}

	// make a new thread
	result = thread_fork("child_p_thread",child,&entry,new_tf,0); // second argument...

	if(result){
		// free new_tf?
		// need double check as_destroy(addrspace)
		// should clean the file array for us: see proc.c
		proc_destroy(child);
		return result;
	}

	child->parent = curproc;
	*retval = child->pid;
	return 0;
}

int
sys_waitpid(pid_t pid, userptr_t ret, int options, pid_t* retval) {
	if (ret == NULL) {
		return EFAULT; // Invalid pointer
	}
	if (pid < __PID_MIN || pid > __PID_MAX) {
		return ESRCH; // Invalid PID
	}
	if (options) {
		return EINVAL; // We don't support any options
	}

	int err;

	P(pidTableLock);

	// Check if valid
	struct proc* p = pidTable[pid];
	if (p == NULL) {
		V(pidTableLock);
		return ESRCH; // No process
	}

	if (p->parent != curproc) {
		V(pidTableLock);
		return ECHILD; // Can only wait on children
	}

	// Check if proc already done (then no need to wait), and release table lock
	if (p->isDone) {
		err = copyout((void*)&p->exitCode, ret, sizeof(int));
		V(pidTableLock);
		if (err) {
			// Do not set return value on error
			return err;
		}
		*retval = pid;
		return 0;
	}

	//ensure wait and destroy process are mutual exclusive, can multiple threads wait
	rw_wait(p->wait_rw_lock, (RoW)0);
	//no access to pid table, release the lock
	V(pidTableLock);
	// Now wait on the child's semaphore
	P(p->parentWait);
	err = copyout((void*)&p->exitCode, ret, sizeof(int));
	// Once we have the semaphore, just release it and return
	// since the return value has already been sent
	V(p->parentWait);
	//release the readlock
	rw_signal(p->wait_rw_lock, (RoW)0);

	if (err) {
		// Do not set return value on error
		return err;
	}

	// return value
	*retval = pid;
	return 0;
}

/*
 * Takes in a string that is the program name (user_prog_name),
 * an array of strings that are arguments (user_args) and an address
 * at which to save the return value.
 *
 * Note that user_prog_name and user_args both live in userspace.
 * user_prog_name: char*
 * user_args: char**
 */
int sys_execv(userptr_t user_prog_name, userptr_t user_args, int* retval) {
	int err = 0;

	// Check that pointers aren't NULL
	if (user_prog_name == NULL || user_args == NULL) {
		*retval = -1;
		return EFAULT;
	}

	// Buffer to copy program name to (from userspace to kernel space)
	char program_name[PATH_MAX + 1];
	program_name[PATH_MAX] = '\0'; // paranoia

	size_t len;
	err = copyinstr(user_prog_name, program_name, PATH_MAX, &len);
	// Copy failed - invalid pointer or other
	if (err) {
		*retval = -1;
		return err;
	}
	// Name was empty string, which is a non-existent file
	if (strlen(program_name) == 0) {
		*retval = -1;
		return EINVAL;
	}

	// Check if args is a valid pointer in userspace, in a jokes way
	char testByte;
	err = copyin((userptr_t)user_args, &testByte, sizeof(char));
	if (err) return err;

	// TODO: We need to check if *all* of the args are valid pointers
	// (but this edge case may not come up in tests)

	// Find the nargs
	unsigned int nargs = 0;
	userptr_t* user_arg_arr = (userptr_t*)user_args;
	while(user_arg_arr[nargs] != NULL) {
		// Not necessary checking here
		//err = copyin((userptr_t)user_arg_arr[nargs], &testByte, sizeof(char))
		//if(err) return err;
		nargs += 1;
	}

	// malloc size of argv
	char** argv = kmalloc(sizeof(char*) * (nargs + 1));
	if (argv == NULL) {
		*retval = -1;
		return ENOMEM;
	}

	// Where we will copy to (on the *stack*)
	char arg[ARGUMENT_SIZE_MAX + 1];
	arg[ARGUMENT_SIZE_MAX] = '\0';

	unsigned int actual_arg_size = 0;
	for(unsigned int i = 0; i < nargs; i++){
		err = copyinstr(user_arg_arr[i], arg, ARGUMENT_SIZE_MAX, &len);

		if (err) {
			// Free everything previous and return result
			for (unsigned int j = 0; j < i; ++j) kfree(argv[j]);
			kfree(argv);
			*retval = -1;
			return err;
		}

		actual_arg_size += len;
		if (actual_arg_size > ARGUMENT_SIZE_MAX) {
			// Free everything previous and return result
			for (unsigned int j = 0; j < i; ++j) kfree(argv[j]);
			kfree(argv);
			*retval = -1;
			return E2BIG;
		}

		argv[i] = kmalloc(sizeof(char)*(len + 1));
		if (argv[i] == NULL) {
			// Free everything previous and return ENOMEM
			for (unsigned int j = 0; j < i; ++j) kfree(argv[j]);
			kfree(argv);
			*retval = -1;
			return ENOMEM;
		}
		argv[i][len] = '\0'; // paranoia

		// Copy arg to argv[i]
		strcpy(argv[i], arg);
		// TODO: use strcpy ?
		//for (unsigned int j = 0; arg[j]; ++j) {
		//	argv[i][j] = arg[j];
		//}
	}

	as_destroy(curproc->p_addrspace);
	curproc->p_addrspace = NULL;

	// execute the program
	// TODO: Do we actually have to kmalloc?
	// Can we leave it on the stack?
	err = runprogram(program_name, nargs, argv);

	// Free the things
	for (unsigned int i = 0; i < nargs; ++i) kfree(argv[i]);
	kfree(argv);

	// runprogram should not return
	*retval = -1;
	return err;
}

#endif /* OPT_A2 */

