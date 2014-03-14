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

#include <synch.h>
#include <machine/trapframe.h>
#include <kern/limits.h>
#include <copyinout.h>
#include <test.h>
void entry(void* data1, unsigned long data2);

#endif /* OPT_A2 */

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

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

	//update the fields of the current process
        curproc->exitCode = exitcode;
        curproc->isDone = true;
//	if(curproc->codePtr != NULL){
//		*(curproc->codePtr) = exitcode;
//	}

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
	tf->tf_a3 = 1; // no error
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
	*retval = child->pid; // TODO: Different ret val for parent & child
	return 0;
}

int
sys_waitpid(pid_t pid, int* ret, int options, pid_t* retval) {
	if (ret == NULL) {
		return EFAULT; // Invalid pointer
	}
	if (pid < __PID_MIN || pid > __PID_MAX) {
		return ESRCH; // Invalid PID
	}
	if (options) {
		return EINVAL; // We don't support any options
	}

	P(pidTableLock);

	// Check if valid
	struct proc* p = pidTable[pid];
	if (p == NULL) {
		return ESRCH; // No process
	}

	if (p->parent != curproc) {
		return ECHILD; // Can only wait on children
	}

	// Check if proc already done (then no need to wait), and release table lock
	if (p->isDone) {
		*ret = p->exitCode;
		V(pidTableLock);
		return 0;
	}

	//ensure wait and destroy process are mutual exclusive, can multiple threads wait
	rw_wait(p->wait_rw_lock, (RoW)0);
	//no access to pid table, release the lock
	V(pidTableLock);
	// Now wait on the child's semaphore
	P(p->parentWait);
	*ret = p->exitCode;
	// Once we have the semaphore, just release it and return
	// since the return value has already been sent
	V(p->parentWait);
	//release the readlock
	rw_signal(p->wait_rw_lock, (RoW)0);

	//return
	*retval = pid;
	return 0;
}

int sys_execv(const char *program, char **args){
	int result;

	//no invalid pointer
	if(program == NULL || args == NULL){
		return EFAULT;
	}

	//program name
	char progname[128];
	for(int i = 0; i < 128; i++){
		progname[i] = '\0';
	}

	result = copyin((const_userptr_t)program, progname, sizeof(char) * (strlen(program) + 1));
	//fail
	if(result){
		return result;
	}
	//no program name
	if(strlen(progname) == 0){
		return ENOEXEC;
	}
	//the size of progname is too long
	if(progname[127] != '\0'){
		return ENOEXEC;
	}

	//intial argv
	unsigned long nargs = 0;
	char** argv;
	//check the total size of argument strings
	int arguments_size = 0;

	//find the nargs
	while(args[nargs] != NULL){
		nargs += 1;
	}

	//malloc size of argv
	argv = kmalloc(sizeof(char*) *  (nargs + 1));

	for(unsigned long i = 0; i < nargs; i++){
		int len = strlen(args[i]) + 1;
		argv[i] = kmalloc(sizeof(char) * len);
		result = copyin((const_userptr_t)args[i], argv[i], sizeof(char) * len);
		if(result) return result;
		if((arguments_size + len) > __ARGUMENT_SIZE_MAX){
			return E2BIG;
		}
		arguments_size += len;
	}

	as_destroy(curproc->p_addrspace);
	curproc->p_addrspace = NULL;

	//execute the program
	result = runprogram(progname, nargs, argv);
	if(result) return result;

	//success
	return 0;
}

#endif /* OPT_A2 */

