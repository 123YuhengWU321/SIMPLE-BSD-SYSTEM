#include <types.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <limits.h>
#include <lib.h>
#include <array.h>
#include <clock.h>
#include <thread.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <pid.h>

/**
 *Getter method for our pid
 *returns the ptable[pid] process
 */
struct pid *
pid_get_at_index(pid_t pid)
{
	/**
	 * Make sure the pid is valid by asserting it holds the mutexlock
	 * in range bewtween 1 and MAX since 0,1 are reserved
	 */
	KASSERT(pid >= 1 && pid <= PID_MAX);
	KASSERT(lock_do_i_hold(ptable_lk));

	struct pid *result = ptable[pid % PID_MAX];
	if (result == NULL || result->pid != pid) {
		return NULL;
	}

	return result;
}

/**
 * create a new struct pid with pid_t pid, pid_t ppid, and placed at ptable[pid]
 * we have a mutexlock proetcting it
 */
void
pid_create_at_index(pid_t pid, pid_t ppid){
    
	KASSERT(pid != init);

	struct pid * new_pid = kmalloc(sizeof(struct pid));

    if(new_pid == NULL){
        ptable[pid] = NULL;
    }
	
	/**
	 * Initlizations note the isExited field and exit_status field
	 * 
	 */
    new_pid->pid = pid;
    new_pid->ppid = ppid;
    new_pid->isExited = 0;      //not exited
    new_pid->exit_status = -1;  //no state

    new_pid->p_cv = cv_create("pid cv");
    if (new_pid->p_cv == NULL) {
        kfree(new_pid);
        ptable[pid] = NULL;
    }

	ptable[pid] = new_pid;
}

/**
 * Destructor method for the pid, 
 * cleans up all memory at ptable[pid] 
 */
void
pid_destroy_at_index(pid_t pid)
{
	/**
	 * Make sure the pid is valid by asserting it holds the mutexlock
	 * in range bewtween 1 and MAX since 0,1 are reserved
	 */
	KASSERT(pid >= PID_MIN && pid <= PID_MAX);
    KASSERT(lock_do_i_hold(ptable_lk));
   
    /**
     * Make sure the fields align with the 
     * right pid info
     */
    KASSERT(ptable[pid] != NULL);
    KASSERT(ptable[pid]->pid == pid);
	KASSERT(ptable[pid]->isExited == 1);

    cv_destroy(ptable[pid]->p_cv);
    kfree(ptable[pid]);

    ptable[pid] = NULL;
}

/**
 * Bootstrap like meothod called in main
 * Initlize the ptable when starting os161
 */
void
ptable_init(void)
{
    ptable_lk = lock_create("ptable_lk");

    if (ptable_lk == NULL) {
		panic("failed to create ptable_lk, no memory\n");
	}

	//index 0 and 1 are reserved
    for(int i = PID_MIN; i <= PID_MAX; i++){
        ptable[i] = NULL;
    }

	pid_create_at_index(kern, init);

	if (ptable[kern] == NULL) {
		panic("failed to create kernel pid, no memory\n");
	}
}

/**
 * Constructor method for our actual ptable
 * save the info temporarlily in ret_my_pid on creation
 *  
 *Success returns 0 otherwise returns error
 */
int
pid_create(pid_t *ret_my_pid)
{
    KASSERT(curproc->p_pid != init);

    pid_t curproc_pid = curproc->p_pid;
    
	lock_acquire(ptable_lk);

	/**
	 * 0,1 again are reserved as special pids
	 * make sure that pid are not intilized with prefilled values already
	 * each entry must be unqiue
	 */
    for(int i = 2; i <= PID_MAX; i++){
        if(ptable[i] == NULL){
            pid_create_at_index(i ,curproc_pid);
            if(ptable[i] == NULL){
                lock_release(ptable_lk);
                return ENOMEM;
            }
            
            lock_release(ptable_lk);
            *ret_my_pid = i;

            return 0;
        }
    }

    lock_release(ptable_lk);

    *ret_my_pid = -1;

    return EAGAIN;
}


/**
 * Destructor method for our ptable
 * Balance our the constructor method
 */
void
pid_destroy(pid_t pid)
{
	KASSERT(pid >= PID_MIN && pid <= PID_MAX);
	lock_acquire(ptable_lk);

	struct pid * target = pid_get_at_index(pid);

	KASSERT(target != NULL);
	KASSERT(target->isExited == false);
	KASSERT(target->ppid == curproc->p_pid);

	/* keep pid_destroy from complaining */
	target->exit_status = 0xdead;
	target->isExited = 1;
	target->ppid = init;

	pid_destroy_at_index(pid);

	lock_release(ptable_lk);
}

/**
 * Helper moetod for our exit syscall
 * Make sure parent is not exited unless
 * all its child are exite, in case of an 
 * oprphan process should self-destruct
 */
void
pid_set_exit_status(int status)
{
	lock_acquire(ptable_lk);

	/*Get a valid process as parent*/
	pid_t curproc_pid = curproc->p_pid;
	KASSERT(curproc_pid != init);

	for (int i = PID_MIN; i <= PID_MAX; i++) {
		/*Let all child be destrcuted first before letting parent exit*/
		if (ptable[i] != NULL && ptable[i]->ppid == curproc_pid) {
			ptable[i]->ppid = init;
			if (ptable[i]->isExited == 1) {
				pid_destroy_at_index(ptable[i]->pid);
			}
		}
	}

	/* wake parent up and make it ready to exit*/
	struct pid *parent = pid_get_at_index(curproc_pid);
	KASSERT(parent != NULL);

	parent->exit_status = status;
	parent->isExited = 1;

	if (parent->ppid == init) {
		/**
		 * In the case of an orphan the process should self destuct
		 */
		pid_destroy_at_index(curproc_pid);
	}
	else {
		/*If it is a regular parent process, wakeup all other child before exit*/
		cv_broadcast(parent->p_cv, ptable_lk);
	}

	curproc->p_pid = init;
	lock_release(ptable_lk);
}

/**
 * Helper function for sys_waitpid
 */
int
pid_wait(pid_t pid, int *status)
{	
	lock_acquire(ptable_lk);

	/**
	 * Curproc is the current process' pid and work pid should be its child
	 * 
	 */
	struct pid* working_pid = pid_get_at_index(pid);
	struct pid* curproc_pid = pid_get_at_index(curproc->p_pid);

	/*Nonexistent child/parent pids*/
	if(working_pid == NULL || curproc_pid == NULL){
		lock_release(ptable_lk);
		return ESRCH;
	}

    /*The pid argument named a process that was not a child of the current process.*/
	if(working_pid->ppid != curproc_pid->pid){
		lock_release(ptable_lk);
		return ECHILD;
	}

	/*Parent need to wait for all child to exit*/
	if (working_pid->isExited == false) {
		cv_wait(working_pid->p_cv, ptable_lk);
	}
	
	// if(status == NULL){
	// 	lock_release(ptable_lk);
	// 	return EFAULT;
	// }else{
	// 	*status = working_pid->exit_status;
	// }
	if(status != NULL){
		*status = working_pid->exit_status;
	}
	lock_release(ptable_lk);

	return 0;
}