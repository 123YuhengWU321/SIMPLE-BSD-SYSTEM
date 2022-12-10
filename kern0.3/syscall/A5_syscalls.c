#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <addrspace.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <pid.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <open_file.h>
#include <filetable.h>
#include <syscall.h>
#include <kern/iovec.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <limits.h>
#include <mips/trapframe.h>
#include <execv_helper.h>

/**
 * getpid returns the process id of the current process.
 * 
 * Error:
 * getpid does not fail.
 * 
 */
int
sys_getpid(pid_t *ret)
{
	*ret = curproc->p_pid;
	return 0;
}

/**
 *Helper method when invoking thread fork
 *Used in sys_fork only
 */
static
void 
enter_forked_process_helper(void *tf_ptr, unsigned long dummby)
{
	/*dummby argument for calling thread_fork*/
	(void)dummby;

	/*Copies parent's trapframe to child and free the memory of the temp tf_ptr*/
	struct trapframe childtf = * (struct trapframe *) tf_ptr;
	kfree(tf_ptr);

	/*child enters user mode for first time*/
	enter_forked_process(&childtf);
}

/**
 * fork duplicates the currently running process. The two copies are identical, 
 * except that one (the "new" one, or "child"), has a new, unique process id, and in the other (the "parent") the process id is unchanged.
 * The process id must be greater than 0.
 * The two processes do not share memory or open file tables; this state is copied into the new process, 
 * and subsequent modification in one process does not affect the other.
 * However, the file handle objects the file tables point to are shared, so, for instance, calls to lseek in one process can affect the other.
 * 
 *
 * Return Values:
 * On success, fork returns twice, once in the parent process and once in the child process. In the child process, 0 is returned. In the parent process, the process id of the new child process is returned.
 * On error, no new process is created. fork, only returns once, returning -1, and errno is set according to the error encountered. 
 * 
 * Error Nums:
 * EMPROC	The current user already has too many processes.
 * ENPROC	There are already too many processes on the system.
 * ENOMEM	Sufficient virtual memory for the new process was not available.
 */
int
sys_fork
(struct trapframe *tf, pid_t *retval)
{
	*retval = -1;
	int result;

	/**
	 * These are all filed for the child process
	 * It need to copy the trapframe, filetable and address space of its parent
	 */
	struct proc *childproc;
	struct trapframe *copy_tf;
	struct filetable *child_ft;
	struct addrspace *child_as;

	/*Initlize tf for child which copies parent's tf*/
	copy_tf = kmalloc(sizeof(struct trapframe));
	if (copy_tf==NULL) {
		return ENOMEM;
	}
	memcpy(copy_tf, tf, sizeof(struct trapframe));
	
	/*Instantiate the child process*/
	childproc = proc_create("child_proc");
	if (childproc == NULL) {
		kfree(copy_tf);
		return ENOMEM;
	}

	/*Get the process id for the child process*/
	result = pid_create(&childproc->p_pid);
	if (result) {
		kfree(copy_tf);
		proc_destroy(childproc);
		return result;
	}

	/*Need to copy the parent's address space contebt first*/
	child_as = proc_getas();
	if (child_as != NULL) {
		result = as_copy(child_as, &childproc->p_addrspace);
		if (result) {
			kfree(copy_tf);
			pid_destroy(childproc->p_pid);
			childproc->p_pid = init;      /*Marking the pid as invalid*/
			proc_destroy(childproc);
			return result;
		}
	}

	/*Copy parent's filetable info*/
	child_ft = curproc->p_filetable;
	if (child_ft != NULL) {
		result = filetable_copy(child_ft, &childproc->p_filetable);
		if (result) {
			kfree(copy_tf);
			as_destroy(childproc->p_addrspace);
			childproc->p_addrspace = NULL;
			pid_destroy(childproc->p_pid);
			childproc->p_pid = init;      /*Marking the pid as invalid*/
			proc_destroy(childproc);
			return result;
		}
	}

	/*Set child working directory to the current working directory, this operation is atmoic*/
	/*Implementation adpated from kern/vfs/vfscwd.c and proc_create_runprogram*/
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		childproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	/*Keep the current child process's pid as reference*/
	*retval = childproc->p_pid;

	/*Call the thread_fork method to do actual fork*/
	result = thread_fork("child_proc", 
						 childproc,
			    		 enter_forked_process_helper, 
						 copy_tf,	
						 0);
	if (result) {
		kfree(copy_tf);
		pid_destroy(childproc->p_pid);
		childproc->p_pid = init;      /*Marking the pid as invalid*/
		proc_destroy(childproc);
		return result;
	}

	return 0;
}


/**
 * Cause the current process to exit. The exit code exitcode is reported back to other process(es) via the waitpid() call. 
 * The process id of the exiting process should not be reused until all processes expected to collect the exit code with waitpid have done so. 
 */
void
sys__exit(int status)
{	
	/*Ensure status is not due to a crash*/
	status = _MKWAIT_EXIT(status);
	struct proc *tmp_cur_proc = curproc;

	/**
	 * Set the exit status, wake up any other waiting processes
	 * Parent should exit if and only if all its child have been 
	 * waken up
	 */
	pid_set_exit_status(status);

	/*After waking up all child, the parent's thread should be kernel only*/
	proc_remthread(curthread);
	proc_addthread(kproc, curthread);

	/*Clean up the memory of the buffer process*/
	proc_destroy(tmp_cur_proc);

	
	/*detach the thread by calling thread_exit*/
	thread_exit();
}

/**
 * Wait for the process specified by pid to exit, and return an encoded exit status in the integer pointed to by status. 
 * If that process has exited already, waitpid returns immediately. If that process does not exist, waitpid fails.
 * 
* It is explicitly allowed for status to be NULL, in which case waitpid operates normally but the status value is not produced.
* 
* The options argument should be 0. 
* 
* Errors:
* 	EINVAL	The options argument requested invalid or unsupported options.
*   ECHILD	The pid argument named a process that was not a child of the current process.
*   ESRCH	The pid argument named a nonexistent process.
*   EFAULT	The status argument was an invalid pointer.
 */
int
sys_waitpid(pid_t pid, int * status, int options, pid_t *ret)
{	
	int exit_status; /*This is the kenerl side of the exit _status, user side is the argument*/
	int error;

	/*Invalid PID*/
	if(pid < 1 || pid > PID_MAX){
		return ESRCH;
	}
	
	// if(status == NULL) {
    //     return EFAULT;
    // }

	/*The options argument should be 0.*/
	if (options != 0) {
        return EINVAL;
    }

	/*Parent process waiting for itself*/
	if(pid == curproc->p_pid) {
		return EINVAL;
	}

    /*Wait using the given status, if NULL use NULL as status*/
    if(status == NULL){
        error = pid_wait(pid, NULL);
        if (error) {
            return error;
        }
    }else{
        error = pid_wait(pid, &exit_status);
        if (error) {
            return error;
        }  
    }
	
    /*Copy out exit status to user level*/
	error = copyout(&exit_status, (userptr_t) status, sizeof(int));
	if(error){
		return error;
	}

    /*Keep pid as a reference*/
	*ret = pid;
	
	return 0;
}

/**
 *execv replaces the currently executing program with a newly loaded program image. This occurs within one process; the process id is unchanged.
 *The pathname of the program to run is passed as program. The args argument is an array of 0-terminated strings. The array itself should be terminated by a NULL pointer.
 *The argument strings should be copied into the new process as the new process's argv[] array. In the new process, argv[argc] must be NULL.
 *By convention, argv[0] in new processes contains the name that was used to invoke the program. This is not necessarily the same as program, and furthermore is only a convention and should not be enforced by the kernel.
 *The process file table and current working directory are not modified by execv.
 *The execve call is the same as execv except that a NULL-terminated list of environment strings (of the form var=value) is also passed through. In Unix, execv is a small wrapper for execve that supplies the current process environment. In OS/161, execv is the primary exec call and execve is not supported or needed unless you put in extra work to implement it.
 *The maximum total size of the argv (and environment, if any) data is given by the system constant ARG_MAX. This comes set to 64K by default. You may change this limit, but don't reduce it without asking your course staff. The fact that argv blocks can be large is part of the design problem; while it's possible to set the limit to 4K and still have most things work, you are probably supposed to put at least some thought into engineering a real solution. (One problem to consider is that after the system has been up a while and system memory starts to become fragmented, you still need to be able to allocate enough memory to handle exec. Another is to make sure the system doesn't choke if too many processes are trying to exec at once. There are lots of ways to tackle this; be creative.)
 *Whether the size of the pointers appearing in the argv array count towards the ARG_MAX limit is implementation-defined. Either way it should be possible to pass a lot of small arguments without bumping into some other limit on the number of pointers. 
 * 
 *
 * 
 * On success, execv does not return; instead, the new program begins executing. On failure, execv returns -1, and sets errno to a suitable error code for the error condition encountered.
 * 
 * On Error:
 * ENODEV	The device prefix of program did not exist.
 * ENOTDIR	A non-final component of program was not a directory.
 * ENOENT	program did not exist.
 * EISDIR	program is a directory.
 * ENOEXEC	program is not in a recognizable executable file format, was for the wrong platform, or contained invalid fields.
 * ENOMEM	Insufficient virtual memory is available.
 * E2BIG	The total size of the argument strings exceeeds ARG_MAX.
 * EIO	A hard I/O Error occurred.
 * EFAULT	One of the arguments is an invalid pointer.
 */
int sys_execv(const char *program, char **args)
{  
    int err, argc, len, bufsize;
    len=0;
    bufsize = 0;

    char **arg_ptr;
    char *progname;
    struct vnode *prog_vn;

    char **stack_arg_arr;

	/*Invalid args or pointers*/
	if(program == NULL){
        return EFAULT;
    }
	
    if(args == NULL){
        return EFAULT;
    }

    arg_ptr = kmalloc(sizeof(char*));
	if(arg_ptr == NULL){
		return ENOMEM;
	}

	/*Copy in the user level info into a pointer for later*/
    err = copyin((userptr_t) args, arg_ptr, sizeof(char**));
    if (err) {
        kfree(arg_ptr);
        return err;
    }
    
	argc = 0;
    err = check_arg_length(args, &argc);
    if (err) {
        kfree(arg_ptr);
        return err;
    }

    /*Using arc+1 as we need to reserve the temminating '\0'*/
    char **arg_buf = kmalloc(sizeof(char*) * (argc+1));
    if (arg_buf == NULL) {
        kfree(arg_ptr);
        return ENOMEM;
    }

    /*The temporary destination for argument copyin*/
    char *tmp_arg_dest = kmalloc(sizeof(char) * ARG_MAX);
    if (tmp_arg_dest == NULL) {
        kfree(arg_ptr);
        kfree(arg_buf);
        return ENOMEM;
    }

    /****************************Step One: COPY the arguments to kenerl****************************************************/
    err = copyin_arg(&bufsize, argc, args, arg_ptr, arg_buf, tmp_arg_dest);
    if(err){
        return err;
    }

    kfree(tmp_arg_dest);
    kfree(arg_ptr);

    /****************************Step Two: COPY the prpgoram name to kenerl****************************************************/
    progname = kmalloc(PATH_MAX);
    if (progname == NULL) {
        for (int i = 0; i < argc; i++) {
            kfree(arg_buf[i]);
        }
        kfree(arg_buf);
        return ENOMEM;
    }

    /*Copyin the actual program content*/
    err = copyinstr((userptr_t) program, progname, PATH_MAX, NULL);
    if (err) {
        kfree(progname);
        for (int i = 0; i < argc; i++) {
                kfree(arg_buf[i]);
            }
        kfree(arg_buf);
        return err;
    }

    /*Open the prorgram with the progname using vfs open*/
    err = vfs_open(progname, O_RDONLY, 0, &prog_vn);
    if (err) {
        kfree(progname);
        for (int i = 0; i < argc; i++) {
                kfree(arg_buf[i]);
            }
        kfree(arg_buf);  
        return err;
    }
    kfree(progname);

    /****************************Step Three: Deal with as and user stack****************************************************/
    struct addrspace *as_new = as_create();
    if (as_new == NULL) {
        for (int i = 0; i < argc; i++) {
            kfree(arg_buf[i]);
        }
        kfree(arg_buf); 
        return ENOMEM;
    }

    /*Change to newly created address space*/
    as_deactivate();
    struct addrspace *as_old = proc_setas(as_new);
    as_activate();

    /*use load elf to load the executable, with the entry points*/
    vaddr_t entrypoint;
    err = load_elf(prog_vn, &entrypoint);
    if (err) {
        goto stack_err;
    }

    /*set region for new stack */
    vaddr_t stackptr;
    err = as_define_stack(as_new, &stackptr);
    if (err) {
      goto stack_err_nomem;
    }

    stack_arg_arr = kmalloc (sizeof(char *) * (argc + 1));
    if (stack_arg_arr == NULL) {
        goto stack_err_nomem;
    }
    
    /*Move stack pointer to bottom of the buffer*/
    stackptr -= bufsize;

    /*Update the stack pointer*/
    err = update_stack_pointer(&stackptr, argc, len, arg_buf, stack_arg_arr);
    if (err) {
        goto stack_err;
    }

    /*Reaching the end and clean up memory*/
    stack_arg_arr[argc] = 0;
    for (int i = 0; i < argc; i++) {
        kfree(arg_buf[i]);
    }
    kfree(arg_buf);

    /*Update stack pointer before copying out the arguments in the stack arguments array*/
    stackptr = stackptr - (argc + 1) * (sizeof(char *));
    stackptr = stackptr - bufsize;

    /*Copy out the content from kernel spcace to user space*/
    err = copyout_arg(&stackptr, argc, stack_arg_arr);
    if (err) {
        proc_setas(as_old);
        as_activate();
        as_destroy(as_new);
        vfs_close(prog_vn);
        kfree(stack_arg_arr);
        return err;
    }

    kfree(stack_arg_arr);

    /*Update the location of the stack pointer*/
    stackptr = stackptr - (argc + 1) * (sizeof(char *));

    as_destroy(as_old);

    /*Enter new process should not return*/
    enter_new_process(argc, (userptr_t) stackptr, NULL, (vaddr_t) stackptr, entrypoint);
    panic("Enter new process should not have returened but it returned.\n");

    return -1;

stack_err:
    for (int i = 0; i < argc; i++) {
        kfree(arg_buf[i]);
    }
    kfree(arg_buf); 
    proc_setas(as_old);
    as_activate();
    as_destroy(as_new);
    vfs_close(prog_vn);
    return err;

stack_err_nomem:
    for (int i = 0; i < argc; i++) {
        kfree(arg_buf[i]);
    }
    kfree(arg_buf);
    proc_setas(as_old);
    as_activate();
    as_destroy(as_new);
    vfs_close(prog_vn);
    return ENOMEM;
}