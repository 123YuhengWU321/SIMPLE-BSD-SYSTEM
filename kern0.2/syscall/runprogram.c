#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <open_file.h>
#include <filetable.h>

/**
 * This is a helper function to define the stdin, out and err
 * for file descriptors 0, 1 and 2.
 * The path is the console and using mode 0664 to be consistent with
 * the fsyscall test.
 * 
 * On success return 0; else return error
 */
static int
init_std012()
{
	struct open_file *stdin, *stdout, *stderr;
	int result;
	
	/**
	 * Handling each case accordingly,
	 * stdin uses fd STDIN_FILENO which is 0 and the read only flag
	 * stdut/err uses STDOUT/ERR_FILENO (which is 1 and 2) and write only flag
	 * 
	 * On creadting these open file entries we set the current process's 
	 * open file entry accordingly
	 */
	result = open_file_open(kstrdup("con:"), O_RDONLY, 0664, &stdin);
	if (result)
	{
		return result;
	}
	curproc->p_filetable->entries[STDIN_FILENO] = stdin;

	result = open_file_open(kstrdup("con:"), O_WRONLY, 0664, &stdout);
	if (result)
	{
		return result;
	}
	curproc->p_filetable->entries[STDOUT_FILENO] = stdout;

	result = open_file_open(kstrdup("con:"), O_WRONLY, 0664, &stderr);
	if (result)
	{
		return result;
	}
	curproc->p_filetable->entries[STDERR_FILENO] = stderr;

	return 0;
}


/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	
	/**
	 * Reserving the special fds for the three stds
	 */
	if (curproc->p_filetable == NULL){
		curproc->p_filetable = ft_create();
		if (curproc->p_filetable == NULL){
			vfs_close(v);
			return ENOMEM;
		}
		
	}

	result = init_std012();
	if (result){
		vfs_close(v);
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

