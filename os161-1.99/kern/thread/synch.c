/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

#include "opt-A1.h"
#include "opt-A2.h"

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, int initial_count)
{
	struct semaphore *sem;

	KASSERT(initial_count >= 0);

	sem = kmalloc(sizeof(struct semaphore));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 * Bridge to the wchan lock, so if someone else comes
		 * along in V right this instant the wakeup can't go
		 * through on the wchan until we've finished going to
		 * sleep. Note that wchan_sleep unlocks the wchan.
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_lock(sem->sem_wchan);
		spinlock_release(&sem->sem_lock);
		wchan_sleep(sem->sem_wchan);

		spinlock_acquire(&sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}

#if OPT_A1
	// Initialize wait channel
	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		// Not enough memory to create lock
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}

	lock->owner = NULL;

	spinlock_init(&lock->lk_spinlock);
#endif /* OPT_A1 */

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);

#if OPT_A1
	// Clean up spinlock and wait channel
	spinlock_cleanup(&lock->lk_spinlock);
	wchan_destroy(lock->lk_wchan);
#endif /* OPT_A1 */

	kfree(lock->lk_name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
#if OPT_A1
	KASSERT(lock);
	// Don't wait on own lock!
	KASSERT(curthread != lock->owner);

	spinlock_acquire(&lock->lk_spinlock);

	// Wait for the lock
	while (lock->owner) {
		wchan_lock(lock->lk_wchan);
		spinlock_release(&lock->lk_spinlock);
		wchan_sleep(lock->lk_wchan);

		spinlock_acquire(&lock->lk_spinlock);
	}
	KASSERT(!lock->owner);

	// Lock the lock and remember the owner
	lock->owner = curthread;
	KASSERT(lock->owner);

	spinlock_release(&lock->lk_spinlock);

#else
	(void)lock;  // suppress warning until code gets written
#endif /* OPT_A1 */
}

void
lock_release(struct lock *lock)
{
#if OPT_A1
	KASSERT(lock);
	// We can only release a lock that we own
	KASSERT(curthread == lock->owner);

	spinlock_acquire(&lock->lk_spinlock);

	// Release the lock
	lock->owner = NULL;
	KASSERT(!lock->owner);

	// Wake a thread waiting for this lock
	wchan_wakeone(lock->lk_wchan);
	spinlock_release(&lock->lk_spinlock);

#else
	(void)lock;  // suppress warning until code gets written
#endif /* OPT_A1 */
}

bool
lock_do_i_hold(struct lock *lock)
{
#if OPT_A1
	KASSERT(lock);
	return curthread == lock->owner;

#else
	(void)lock;  // suppress warning until code gets written
	return true; // dummy until code gets written
#endif /* OPT_A1 */
}

////////////////////////////////////////////////////////////
//
//  ReadWrite lock
# if OPT_A2
struct rwlock *
rw_create(const char *name)
{
	struct rwlock* rwlock;
	rwlock = kmalloc(sizeof(struct rwlock));
	if(rwlock == NULL){
		return NULL;
	}
	
	rwlock -> name = kstrdup(name);
	if(rwlock -> name == NULL){
		kfree(rwlock);
		return NULL;
	}
	
	rwlock -> mutex = sem_create(name,1); // mutex
	if(rwlock-> mutex == NULL){
		kfree(rwlock->name);
		kfree(rwlock);
		return NULL;
	}
	
	rwlock -> readerl = sem_create(name,1); // mutex
	if(rwlock-> readerl == NULL){
		kfree(rwlock->mutex);
		kfree(rwlock->name);
		kfree(rwlock);
		return NULL;
	}
	
	rwlock -> readerc = 0; // no readers initially
	return rwlock;
}

void
rw_wait(struct rwlock* rwlock, RoW READERORWRITER){
	KASSERT(rwlock);
	
	switch (READERORWRITER){
		case READER:
			// acquire readerc
			P(rwlock->readerl);
			rwlock->readerc ++;
			if(rwlock -> readerc == 1){
				P(rwlock->mutex);// no more writers, but readers are ok
			}
			V(rwlock->readerl); // release the read lock, other read may enter
			break;
		case WRITER:
			// acquire the mutex
			P(rwlock -> mutex);
			break;
	}
}

void
rw_signal(struct rwlock* rwlock,RoW READERORWRITER){
	KASSERT(rwlock);
		
	switch(READERORWRITER){
		case READER:
			P(rwlock->readerl);
			rwlock-> readerc --;
			if(rwlock->readerc ==0){
				// release the mutex, no more readers
				V(rwlock->mutex);
			}
			V(rwlock -> readerl);
			break;
		case WRITER:
			V(rwlock-> mutex);
			break;
	}
}

void 
rw_destroy(struct rwlock* rwlock){
	
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->readerc == 0);
	
	sem_destroy(rwlock->mutex);
	sem_destroy(rwlock->readerl);

	kfree(rwlock->name);
	kfree(rwlock);

}

# endif







////////////////////////////////////////////////////////////
//
// CV

struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

#if OPT_A1
	// Initialize wait channel
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		// Not enough memory to create wchan
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}

	spinlock_init(&cv->cv_spinlock);
#endif /* OPT_A1 */

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

#if OPT_A1
	// Clean up spinlock and wait channel
	spinlock_cleanup(&cv->cv_spinlock);
	wchan_destroy(cv->cv_wchan);
#endif /* OPT_A1 */

	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
#if OPT_A1
	KASSERT(cv);
	KASSERT(lock);

	spinlock_acquire(&cv->cv_spinlock);
	// Release the lock
	lock_release(lock);

	// Sleep on wait channel
	wchan_lock(cv->cv_wchan);
	spinlock_release(&cv->cv_spinlock);
	wchan_sleep(cv->cv_wchan);

	// Once awakened, re-acquire lock
	lock_acquire(lock);

#else
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
#endif /* OPT_A1 */
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
#if OPT_A1
	KASSERT(cv);
	KASSERT(lock);

	spinlock_acquire(&cv->cv_spinlock);

	// Wake a thread waiting on this wchan
	wchan_wakeone(cv->cv_wchan);

	spinlock_release(&cv->cv_spinlock);

#else
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
#endif /* OPT_A1 */
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
#if OPT_A1
	KASSERT(cv);
	KASSERT(lock);

	spinlock_acquire(&cv->cv_spinlock);

	// Wake all threads waiting on this cv
	wchan_wakeall(cv->cv_wchan);

	spinlock_release(&cv->cv_spinlock);

#else
	// Write this
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
#endif /* OPT_A1 */
}
