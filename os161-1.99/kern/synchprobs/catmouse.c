/*
 * catmouse.c
 *
 * 30-1-2003 : GWA : Stub functions created for CS161 Asst1.
 * 26-11-2007: KMS : Modified to use cat_eat and mouse_eat
 * 21-04-2009: KMS : modified to use cat_sleep and mouse_sleep
 * 21-04-2009: KMS : added sem_destroy of CatMouseWait
 * 05-01-2012: TBB : added comments to try to clarify use/non use of volatile
 * 22-08-2013: TBB: made cat and mouse eating and sleeping time optional parameters
 *
 */


/*
 *
 * Includes
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>
#include <synchprobs.h>

#include "opt-A1.h"

/*
 *
 * cat,mouse,bowl simulation functions defined in bowls.c
 *
 * For Assignment 1, you should use these functions to
 *  make your cats and mice eat from the bowls.
 *
 * You may *not* modify these functions in any way.
 * They are implemented in a separate file (bowls.c) to remind
 * you that you should not change them.
 *
 * For information about the behaviour and return values
 *  of these functions, see bowls.c
 *
 */

/* this must be called before any calls to cat_eat or mouse_eat */
extern int initialize_bowls(unsigned int bowlcount);

extern void cleanup_bowls( void );
extern void cat_eat(unsigned int bowlnumber, int eat_time );
extern void mouse_eat(unsigned int bowlnumber, int eat_time);
extern void cat_sleep(int sleep_time);
extern void mouse_sleep(int sleep_time);

/*
 *
 * Problem parameters
 *
 * Values for these parameters are set by the main driver
 *  function, catmouse(), based on the problem parameters
 *  that are passed in from the kernel menu command or
 *  kernel command line.
 *
 * Once they have been set, you probably shouldn't be
 *  changing them.
 *
 * These are only ever modified by one thread, at creation time,
 * so they do not need to be volatile.
 */
int NumBowls;  // number of food bowls
int NumCats;   // number of cats
int NumMice;   // number of mice
int NumLoops;  // number of times each cat and mouse should eat

/*
 * Defaults here are as they were with the previous implementation
 * where these could not be changed.
 */
int CatEatTime = 1;      // length of time a cat spends eating
int CatSleepTime = 5;    // length of time a cat spends sleeping
int MouseEatTime = 3;    // length of time a mouse spends eating
int MouseSleepTime = 3;  // length of time a mouse spends sleeping

/*
 * Once the main driver function (catmouse()) has created the cat and mouse
 * simulation threads, it uses this semaphore to block until all of the
 * cat and mouse simulations are finished.
 */
struct semaphore *CatMouseWait;

// Locks for CVs
struct lock* wait_lk = NULL;
struct lock* backlog_lk = NULL;
// Condition variables for queues
struct cv* eat_queue = NULL;
struct cv* backlog_queue = NULL;

// Mutex locks
//struct lock* mutex = NULL;
struct semaphore* mutex = NULL;
struct lock* bowl_lk = NULL;

// Array of bowls taken
bool* bowls = NULL;
// Track number of free bowls
struct semaphore* bowl_sem = NULL;

// Which animal is currently eating
volatile char eating = '-';
// Useful counts
volatile int numEating = 0;
volatile int numWaiting = 0;

/*
 *
 * Function Definitions
 *
 */

int getBowl(void) {
	/* Get an empty bowl.  We assume that there is an empty bowl. */
	lock_acquire(bowl_lk);
	for(int i = 0; i < NumBowls; ++i) {
		if(bowls[i]) {
			bowls[i] = false;
			lock_release(bowl_lk);
			return i + 1;
		}
	}
	panic("Did not find empty bowl.");
	return 0;
}
void freeBowl(int i) {
	/* Free a bowl from use. */
	lock_acquire(bowl_lk);
	bowls[i - 1] = true;
	lock_release(bowl_lk);
}

void backlog_check(char self) {
	/* Wait in the backlog if necessary */
	lock_acquire(wait_lk);
	if (numWaiting && (eating == self)) {
		lock_release(wait_lk);

		lock_acquire(backlog_lk);
		cv_wait(backlog_queue, backlog_lk);
		lock_release(backlog_lk);
		return;
	}
	lock_release(wait_lk);
}

void wait_check(char self) {
	/* Wait to eat, if necessary. */
	char other = (self == 'c') ? 'm' : 'c';

	if (eating == other) {
		// If others eating then wait for signal that all are done
		lock_acquire(wait_lk);
		numWaiting += 1;
		cv_wait(eat_queue, wait_lk);
		numWaiting -= 1;
		// If nobody else is waiting then open the backlog to begin
		// waiting to eat
		if (numWaiting == 0) {
			cv_broadcast(backlog_queue, backlog_lk);
			eating = self; // our turn to eat
		}
		lock_release(wait_lk);
	}
}

void eat_check(char self) {
	/* Eat. */
	int eatTime = (self == 'c') ? CatEatTime : MouseEatTime;
	void(*eat_func)(unsigned int, int) = (self == 'c') ? cat_eat : mouse_eat;

	// Wait for a bowl to be free
	P(bowl_sem);
	//lock_acquire(mutex);
	P(mutex);
	numEating += 1;
	// First one eating
	if (numEating == 1) {
		eating = self;
	}

	// Determine which bowl to eat from
	int bowl = getBowl();
	//lock_release(mutex);
	V(mutex);
	// Actually eat
	(*eat_func)(bowl, eatTime);

	//lock_acquire(mutex);
	P(mutex);
	freeBowl(bowl); // Someone else can use bowl

	V(bowl_sem); // Free up a bowl
	numEating -= 1;
	// Last has finished eating
	if (numEating == 0) {
		eating = '-'; // Something neutral
		// Broadcast that we have finished eating
		cv_broadcast(eat_queue, wait_lk);
	}
	//lock_release(mutex);
	V(mutex);
}

/*
 * cat_simulation()
 */
static
void
cat_simulation(void * unusedpointer, unsigned long catnumber)
{
	/* Avoid unused variable warnings. */
	(void) unusedpointer;
	(void) catnumber;

	for(int i = 0; i < NumLoops; ++i) {
		cat_sleep(CatSleepTime);

		backlog_check('c');
		wait_check('c');
		eat_check('c');
	}

	// Cat finished
	V(CatMouseWait);
}

/*
 * mouse_simulation()
 */
static
void
mouse_simulation(void * unusedpointer, unsigned long mousenumber)
{
	/* Avoid unused variable warnings. */
	(void) unusedpointer;
	(void) mousenumber;

	for(int i = 0; i < NumLoops; ++i) {
		mouse_sleep(MouseSleepTime);

		backlog_check('m');
		wait_check('m');
		eat_check('m');
	}

	// Mouse finished
	V(CatMouseWait);
}


/*
 * catmouse()
 *
 * Arguments:
 *      int nargs: should be 5 or 9
 *      char ** args: args[1] = number of food bowls
 *                    args[2] = number of cats
 *                    args[3] = number of mice
 *                    args[4] = number of loops
 * Optional parameters
 *                    args[5] = cat eating time
 *                    args[6] = cat sleeping time
 *                    args[7] = mouse eating time
 *                    args[8] = mouse sleeping time
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up cat_simulation() and
 *      mouse_simulation() threads.
 *      You may need to modify this function, e.g., to
 *      initialize synchronization primitives used
 *      by the cat and mouse threads.
 *
 *      However, you should should ensure that this function
 *      continues to create the appropriate numbers of
 *      cat and mouse threads, to initialize the simulation,
 *      and to wait for all cats and mice to finish.
 */

int
catmouse(int nargs, char ** args)
{
  int index, error;
  int i;

  /* check and process command line arguments */
  if ((nargs != 9) && (nargs != 5)) {
    kprintf("Usage: <command> NUM_BOWLS NUM_CATS NUM_MICE NUM_LOOPS\n");
    kprintf("or\n");
    kprintf("Usage: <command> NUM_BOWLS NUM_CATS NUM_MICE NUM_LOOPS ");
    kprintf("CAT_EATING_TIME CAT_SLEEPING_TIME MOUSE_EATING_TIME MOUSE_SLEEPING_TIME\n");
    return 1;  // return failure indication
  }

  /* check the problem parameters, and set the global variables */
  NumBowls = atoi(args[1]);
  if (NumBowls <= 0) {
    kprintf("catmouse: invalid number of bowls: %d\n", NumBowls);
    return 1;
  }
  NumCats = atoi(args[2]);
  if (NumCats < 0) {
    kprintf("catmouse: invalid number of cats: %d\n", NumCats);
    return 1;
  }
  NumMice = atoi(args[3]);
  if (NumMice < 0) {
    kprintf("catmouse: invalid number of mice: %d\n", NumMice);
    return 1;
  }
  NumLoops = atoi(args[4]);
  if (NumLoops <= 0) {
    kprintf("catmouse: invalid number of loops: %d\n", NumLoops);
    return 1;
  }

  if (nargs == 9) {
    CatEatTime = atoi(args[5]);
    if (CatEatTime < 0) {
      kprintf("catmouse: invalid cat eating time: %d\n", CatEatTime);
      return 1;
    }

    CatSleepTime = atoi(args[6]);
    if (CatSleepTime < 0) {
      kprintf("catmouse: invalid cat sleeping time: %d\n", CatSleepTime);
      return 1;
    }

    MouseEatTime = atoi(args[7]);
    if (MouseEatTime < 0) {
      kprintf("catmouse: invalid mouse eating time: %d\n", MouseEatTime);
      return 1;
    }

    MouseSleepTime = atoi(args[8]);
    if (MouseSleepTime < 0) {
      kprintf("catmouse: invalid mouse sleeping time: %d\n", MouseSleepTime);
      return 1;
    }
  }

  kprintf("Using %d bowls, %d cats, and %d mice. Looping %d times.\n",
          NumBowls, NumCats, NumMice, NumLoops);
  kprintf("Using cat eating time %d, cat sleeping time %d\n", CatEatTime, CatSleepTime);
  kprintf("Using mouse eating time %d, mouse sleeping time %d\n", MouseEatTime, MouseSleepTime);

  /* create the semaphore that is used to make the main thread
     wait for all of the cats and mice to finish */
  CatMouseWait = sem_create("CatMouseWait",0);
  if (CatMouseWait == NULL) {
	panic("catmouse: could not create semaphore\n");
  }

  /*
   * initialize the bowls
   */
  if (initialize_bowls(NumBowls)) {
    panic("catmouse: error initializing bowls.\n");
  }

  // Initialize the array for tracking taken bowls
  bowls = kmalloc(NumBowls * sizeof(bool));
  for(int b = 0; b < NumBowls; ++b) {
	  bowls[b] = true;
  }
  // Initialize the semaphore for managing bowls
  bowl_sem = sem_create("Bowls", NumBowls);

  // Two locks as mutexes
  //mutex = lock_create("mutex");
  mutex = sem_create("mutex", 1);
  bowl_lk = lock_create("bowl_lk");
  // Two locks for condition variables
  wait_lk = lock_create("wait_lk");
  backlog_lk = lock_create("backlog_lk");
  // Two condition variables for queues
  eat_queue = cv_create("eat_queue");
  backlog_queue = cv_create("backlog_queue");

  /*
   * Start NumCats cat_simulation() threads.
   */
  for (index = 0; index < NumCats; index++) {
    error = thread_fork("cat_simulation thread", NULL, cat_simulation, NULL, index);
    if (error) {
      panic("cat_simulation: thread_fork failed: %s\n", strerror(error));
    }
  }

  /*
   * Start NumMice mouse_simulation() threads.
   */
  for (index = 0; index < NumMice; index++) {
    error = thread_fork("mouse_simulation thread", NULL, mouse_simulation, NULL, index);
    if (error) {
      panic("mouse_simulation: thread_fork failed: %s\n",strerror(error));
    }
  }

  /* wait for all of the cats and mice to finish before
     terminating */
  for(i = 0; i < (NumCats + NumMice); i++) {
    P(CatMouseWait);
  }

  /* clean up the semaphore that we created */
  sem_destroy(CatMouseWait);

  kfree(bowls);
  sem_destroy(bowl_sem);

  // Clean up locks
  //lock_destroy(mutex);
  sem_destroy(mutex);
  lock_destroy(bowl_lk);
  lock_destroy(wait_lk);
  lock_destroy(backlog_lk);
  // Clean up cvs
  cv_destroy(eat_queue);
  cv_destroy(backlog_queue);

  /* clean up resources used for tracking bowl use */
  cleanup_bowls();

  return 0;
}

/*
 * End of catmouse.c
 */
