#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16

//counter for ropes connected 
static int ropes_left = NROPES;
volatile int numDisconnected = 0;

/**
 * thread status for each running thread
 * evaluates to true if and only if 
 * a thread is finished
 */
volatile bool dand_is_finished;
volatile bool mari_is_finished;
volatile bool balloon_is_finished;
//flower killer is special (has clones) so it is a boolean arraybool *flower_is_finished; 
bool *flower_is_finished;

/* Data structures for rope mappings */

/**
 * Both hook and stake are int arrays which maps their index
 * to each of the ropes in ascedning order each index has a boolean  
 * attribute, if it is connected it evalutes to true and false otherwise
 * balloonHook holds the lock for each rope (every index has its own lock)
 */
struct balloonHook {
    volatile int groundIndex;
    volatile bool isConnected;
	struct   lock   *hook_lk;

};

struct groundStake {
    volatile int balloonIndex;
    volatile bool isConnected;
};

/* Sturct Initialization */
static struct balloonHook  *balloonHooks;
static struct groundStake  *groundStakes;

/* Synchronization primitives */
struct lock *mutex_lk;     
struct lock *thread_lk;


/*
 * Dandelion thread keeps checking if the rope mapped to the hook he
 * has access to is detached, if not he will detach the hook and mark
 * the hook as detached as well as the ground stake as detached as well 
 * 
 * Once the rope has been deatached successfully dandelion increment the
 * disconnected count of ropes by 1(decrement ropes left by one),
 * finishes its print statment and yiedls to other 
 * threads and waits until they finish.
 * 
 */
static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	lock_acquire(thread_lk);
	kprintf("Dandelion thread starting\n");
	lock_release(thread_lk);

 	int balloonIndex, groundIndex; 
	while(numDisconnected < NROPES){
		balloonIndex = random() % NROPES;
		//using the random balloonIndex to find the next rope to cut
		while(1){
			if(balloonHooks[balloonIndex].isConnected){
				break;
			}
			balloonIndex++;
			if(balloonIndex >= NROPES){
				balloonIndex = 0;
			}
		}

		lock_acquire(balloonHooks[balloonIndex].hook_lk);

	    //find the mapping of the rope that is detahced, set both hook and stake to false to indicate so
		groundIndex = balloonHooks[balloonIndex].groundIndex;
		balloonHooks[balloonIndex].isConnected = false;
		groundStakes[groundIndex].isConnected  = false;

		//increment disconnected rope by one
		//decrement ropes left by one
		//This is mutual exclusion content so it must be protected by mutex lock
		lock_acquire(mutex_lk);
		++numDisconnected;
		ropes_left--;
		lock_release(mutex_lk);

		kprintf("Dandelion severed rope %d\n", balloonIndex);

		lock_release(balloonHooks[balloonIndex].hook_lk);

		//yield to other threads in modifictaion of the ropes
		thread_yield();
	}

	lock_acquire(thread_lk);

	dand_is_finished = true;
	kprintf("Dandelion thread done\n");

	lock_release(thread_lk);
	
	thread_exit();
}

/**
 * Marigold thread detaches the ropes from the stakes until all ropes
 * are detahced. Once marigold successfully detaches a rope it will
 * mark both the stake and hook attachment as false and increment the
 * disconnetced ropes by one and decrement the ropes left by one.
 * When making changes to the boolean variable of the rope, it will
 * acquire the mutual exclsuion lock first, after the change has been
 * made it will release the lock.
 * 
 * On finishing executing itself, it yiled to other threads and waits 
 * for other threads. 
 * (Overall it is almost the same logic as dandelion thread)
 */
static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	lock_acquire(thread_lk);
	kprintf("Marigold thread starting\n");
	lock_release(thread_lk);

	
 	int balloonIndex, groundIndex; 
	while(numDisconnected < NROPES){
		groundIndex = random() % NROPES;
		//using the random balloonIndex to find the next rope to cut
		while(1){
			if(groundStakes[groundIndex].isConnected){
				break;
			}
			groundIndex++;
			if(groundIndex >= NROPES){
				groundIndex = 0;
			}
		}
		int lockId = groundStakes[groundIndex].balloonIndex;
		lock_acquire(balloonHooks[lockId].hook_lk);


	    //find the mapping of the rope that is detahced, set both hook and stake to false to indicate so
		balloonIndex = groundStakes[groundIndex].balloonIndex;
		balloonHooks[balloonIndex].isConnected = false;
		groundStakes[groundIndex].isConnected  = false;

		//increment disconnected rope by one
		//decrement ropes left by one
		//mutex protects it as it is mutex content
		lock_acquire(mutex_lk);
		++numDisconnected;
		ropes_left--;
		lock_release(mutex_lk);
		
		kprintf("Marigold severed rope %d from stake %d\n", balloonIndex, groundIndex);

		lock_release(balloonHooks[lockId].hook_lk);

		//yield to other threads in modifictaion of the ropes
		thread_yield();
	}

	lock_acquire(thread_lk);

	mari_is_finished = true;
	kprintf("Marigold thread done\n");

	lock_release(thread_lk);
	
	thread_exit();
}

/**
 * Flowerkiller keeps seraching for connected ropes on the 
 * stakes and tries to swap two ropes at a time.  Once he 
 * finds the two ropes available he will switch their index
 * On making a successful switch, it will yield to all other
 * threads and release the mutual exclusion lock.
 * 
 * Once there are no more ropes he can swicth it will finish 
 * its priting statement, wait for all other threads to finish.
 * 
 * Same thing applies to all of its clones
 */
static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	int id = (int)arg;
	
	lock_acquire(thread_lk);
	kprintf("Lord FlowerKiller thread starting\n");
	lock_release(thread_lk);

    int groundSwitch1, groundSwitch2, temp;

	while(numDisconnected < NROPES){

		groundSwitch1 = random() % NROPES;
		// find the first connected stake to swap
		while (1) {
				if (groundStakes[groundSwitch1].isConnected) {
					break;
				}
				groundSwitch1++;
				if (groundSwitch1 >= NROPES){
					groundSwitch1 = 0;
				}
			}
		
		groundSwitch2 = random() % NROPES;
		// find the next connected stake to swap and it has to be different from the first
		while (1) {
				if (groundSwitch2 != groundSwitch1
				    && groundStakes[groundSwitch2].isConnected) {
					break;
				}
				groundSwitch2++;
				if (groundSwitch2 >= NROPES){
					groundSwitch2 = 0;
				}
			}
			
		/**
		 * Mth thread cannot lock and unlock using the groundStakes content.  
		 * Its balloon mapping may be swapped.  
		 * Use lockId to make sure the same lock is locked and unlocked.
		 * order the locks by descending order, first the one with bigger index then the smaller one
		 * this is done to avoid deadlock
		 */
		int lockId1, lockId2;

        if(&balloonHooks[groundStakes[groundSwitch2].balloonIndex].hook_lk > 
		   &balloonHooks[groundStakes[groundSwitch1].balloonIndex].hook_lk){
	  	   lockId1 = groundStakes[groundSwitch2].balloonIndex;
	  	   lockId2 = groundStakes[groundSwitch1].balloonIndex;
		}else{
	       lockId1 = groundStakes[groundSwitch1].balloonIndex;
	       lockId2 = groundStakes[groundSwitch2].balloonIndex;
		}

		lock_acquire(balloonHooks[lockId1].hook_lk);
		lock_acquire(balloonHooks[lockId2].hook_lk);

		// switch balloon hooks mapping index
		balloonHooks[groundStakes[groundSwitch1].balloonIndex].groundIndex = groundSwitch2;
		balloonHooks[groundStakes[groundSwitch2].balloonIndex].groundIndex = groundSwitch1;

		// switch the groundStake mapping
		temp = groundStakes[groundSwitch1].balloonIndex;
		groundStakes[groundSwitch1].balloonIndex = groundStakes[groundSwitch2].balloonIndex;
		groundStakes[groundSwitch2].balloonIndex = temp;

		//flower killer must print 2 times, one for each rope
		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", 
		temp, groundSwitch1, groundSwitch2);
		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n",
		groundStakes[groundSwitch1].balloonIndex, groundSwitch2, groundSwitch1);

		lock_release(balloonHooks[lockId1].hook_lk);
		lock_release(balloonHooks[lockId2].hook_lk);

		//yield to other threads in modifictaion of the ropes
		thread_yield();
		//add one more yield to reduce killer frequency, since scheduling is uneven
		thread_yield();
	}

	lock_acquire(thread_lk);

	//any one of its clone is finished, flag true for that one 
	flower_is_finished[id] = true;
	kprintf("Lord FlowerKiller thread done\n");
	
	lock_release(thread_lk);
	
	thread_exit();
}



/**
 * The balloon thread will wait until all other threads to finish and then
 *  prints out the finishing statment at the end
 *  
*/
static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	lock_acquire(thread_lk);
	kprintf("Balloon thread starting\n");

	while(1) {

	  //wait until all other threads are finished, for flowerkiller
	  //wait until all its clones are finished 
	  bool allKillerFinished = true;
	  int i;
	  for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
	    if (!flower_is_finished[i]) {
	      allKillerFinished = false;
	      break;
	    }
	  }

	  if (allKillerFinished &&
	      dand_is_finished && mari_is_finished)
	    	break;
		lock_release(thread_lk);
		thread_yield();
		lock_acquire(thread_lk);
	}

	balloon_is_finished = true;

	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");

	lock_release(thread_lk);
	thread_exit();
}


/**
 * init data is a self-defined function that intitilizes the data in
 * the ground and stake maps defined and will be invoked in the main
 * thread
 * 
 */
static
void 
initData(void)
{
     /**
      * Initilize the synchroinzation primitives
      */
	 mutex_lk  = lock_create("mutex_lk");
	 thread_lk = lock_create("thread_lk");
	

	 //initilize the thread status
	 dand_is_finished = false;
	 mari_is_finished = false;
	 flower_is_finished = false;
     balloon_is_finished = false;

	 //allocate memory for the structs
	 balloonHooks = (struct balloonHook *) kmalloc(sizeof(struct balloonHook) * NROPES);
	 groundStakes = (struct groundStake *) kmalloc(sizeof(struct groundStake) * NROPES);
	 flower_is_finished = (bool *) kmalloc(sizeof(bool) * N_LORD_FLOWERKILLER);

	 int balloonIndex, groundIndex, i;
     int ropes[NROPES];

	//Redo the flow of prgroam when running airballoon mutiple times
	numDisconnected = 0;
	ropes_left = NROPES;

	//mpas each index of ropes by ascending index
    for (i = 0; i < NROPES; i++) {
		ropes[i] = i;
    }

	/**
	 * initilize the hook and stake maps with their intial
	 * hook to ground or ground to hook mappings
	 * set all connected flag to true
	 */
    for (i = 0; i < NROPES; i++) {
		balloonIndex = i;
		groundIndex = ropes[i];

		balloonHooks[balloonIndex].groundIndex = groundIndex;
		balloonHooks[balloonIndex].isConnected = true;
		balloonHooks[i].hook_lk = lock_create("hook_lk");

		groundStakes[groundIndex].balloonIndex = balloonIndex;
		groundStakes[groundIndex].isConnected = true;
    }

	//initlize all clones of flowerkiller to fasle
    for (i = 0; i < N_LORD_FLOWERKILLER; i++)
      flower_is_finished[i] = false;
}

/**
 *  Main thread waits for all other threads to finish by yiedlding
 *  to them when thre are unfinished, once everything is finished 
 *  the main thread will clean up all memroy and destroy all sychornization
 *  primitives used
 */
int
airballoon(int nargs, char **args)
{
 	int err = 0;
	int i   = 0;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	initData();

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, i);
		if(err)
			goto panic;
	}
	
	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	/**
	 * give up the thread lock and yield to other threads if they 
	 * not yet finished
	 */
	lock_acquire(thread_lk);
	
	while(!dand_is_finished || !mari_is_finished ||
	      !flower_is_finished ||
	      !balloon_is_finished){
		lock_release(thread_lk);
		thread_yield();
		lock_acquire(thread_lk);
	}

	lock_release(thread_lk);
	
	//clean up all memory and destroy all synchorinztion primitives
	kfree(flower_is_finished);
	for (int i = 0; i<NROPES; i++){
	  lock_destroy(balloonHooks[i].hook_lk);
	}
	kfree(balloonHooks);
	kfree(groundStakes);
	lock_destroy(mutex_lk);
	lock_destroy(thread_lk);

	kprintf("Main thread done\n");
	return 0;
}