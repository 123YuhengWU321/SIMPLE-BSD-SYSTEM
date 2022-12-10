#ifndef _PID_H_
#define _PID_H_

#include <limits.h>

/**
 * Special values we use for the PIDS
 * 0 is initilization only, inavlid during normal execution
 * 1 is reserved for kenrel
 * dead is for dead process
 */
#define init	0	
#define kern	1	
#define dead    0xdead

/**
 * each pid has its own pid, 
 * a parent pid ppid
 * its exit status and a cv
 */
struct pid {
	pid_t pid;					
	pid_t ppid;				
	volatile bool isExited;	
	int exit_status;		
	struct cv *p_cv;		
};

/**
 * The ptable is like the filetable in a4,
 * it holds a unqiue process's info for every entry
 * 
 */
struct lock *ptable_lk;		
struct pid *ptable[PID_MAX]; 

/*Initlizer called in main*/
void ptable_init(void);

/*Getter method for a pid*/
struct pid * pid_get_at_index(pid_t pid);

/*pid constructor/destructor*/
void pid_create_at_index(pid_t pid, pid_t ppid);
void pid_destroy_at_index(pid_t pid);

/*ptable constructor/destructor*/
int pid_create(pid_t *ret_my_pid);
void pid_destroy(pid_t pid);

/*Helper for the exit syscall*/
void pid_set_exit_status(int status);
int pid_wait(pid_t pid, int *status);


#endif /* _PID_H_ */
