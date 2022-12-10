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

/**************************************The following four helper functions are all used for execvp syscall only*****************************************************/

/**
 * Helper function for checking the arguments length
 * making sure each entry is valid and argc should not
 * exceed  ARG_MAX
 */
int 
check_arg_length(char **args, int* argc)
{
	while(args[*argc] != NULL && *argc <= ARG_MAX){
		if (*argc > ARG_MAX) {
			return E2BIG;
		}
		*argc+=1;
	}
    return 0;
}

/**
 * Helper method for copyin the arguements
 * to the kenerl space, invoked at step one
 * of the execvp syscall
 * 
 */ 
int
copyin_arg(int *bufsize, int argc, char **args, char **arg_ptr, char **arg_buf, char *tmp_arg_dest){
    int arg_count = 0;
    int err = 0;
    userptr_t tmp_user_src; 
    while(arg_count < argc && args != NULL){
        tmp_user_src = sizeof(char *) * arg_count  + (userptr_t) args;

        /*Copy in the info to a pointer for later use of copyinstr*/
        err = copyin(tmp_user_src, arg_ptr, sizeof(char*));
        if (err) {
            kfree(arg_ptr);
            kfree(arg_buf);
            return err;
        }
        /*Copy the string to tmp_arg_dest*/
        err = copyinstr((userptr_t)args[arg_count], tmp_arg_dest, sizeof(char) * ARG_MAX, NULL);
        if (err) {
            kfree(arg_ptr);
            kfree(arg_buf);
            kfree(tmp_arg_dest);
            return err;
        }
        
        /*Make sure to consider the termination '\0'*/
        int len_tmp = strlen(args[arg_count]) + 1;

        arg_buf[arg_count] = kmalloc(sizeof(char) * len_tmp);
        if (arg_buf[arg_count] == NULL) {
            kfree(arg_ptr);
            for (int i = 0; i < arg_count-1; i++) {
                kfree(arg_buf[i]);
            }
            kfree(arg_buf);
            kfree(tmp_arg_dest);
            return ENOMEM;
        }

        /*Now copyin the string content into the kenerl space*/
        err = copyinstr((userptr_t) args[arg_count], arg_buf[arg_count], sizeof(char) * len_tmp, NULL);
        if (err) {
            kfree(arg_ptr);
            for (int i = 0; i < arg_count; i++) {
                kfree(arg_buf[i]);
            }
            kfree(arg_buf);
            kfree(tmp_arg_dest);
            return err;
        }
        
        /*Align the argument length before adding it to the buffer size*/
        len_tmp = (len_tmp%4 != 0) ? 4*(len_tmp/4)+ 4: len_tmp;
        *bufsize += len_tmp;
        arg_count +=1;
    }
    return err;
}

/**
 * Helper method for copyin the arguements
 * to the user space, invoked at step three
 * of the execvp syscall
 * 
 */
 
int 
copyout_arg(vaddr_t *stackptr, int argc, char **stack_arg_arr){
    int arg_count = 0;
    int err = 0;
    while(arg_count<argc + 1 && (userptr_t) *stackptr != NULL && stack_arg_arr + arg_count != NULL){
        /*Copy information out of the kenerl stack_arg_arr to user stack*/
        err = copyout(stack_arg_arr + arg_count, (userptr_t) *stackptr, sizeof(char *));
        if (err) {
            return err;
        }

        *stackptr += sizeof(char *);
        arg_count +=1;
    }
    return err;
}


/**
 * Helper function that updates the stack pointer
 * making sure that the length of the stack is properly
 * aligned
 * 
 * Invoked in step 3 of execvp syscall
 */
int
update_stack_pointer(vaddr_t *stackptr, int argc, int len, char **arg_buf, char **stack_arg_arr)
{
    int arg_count = 0;
    int len_stack;
    int err = 0;
    while(arg_count<argc){
        len_stack = strlen(arg_buf[arg_count]) + 1;
        /*Copy our the infomration from stack to user stack*/
        err = copyoutstr(arg_buf[arg_count], (userptr_t) *stackptr, len_stack, NULL);
        if(err){
            return err;
        }
        
        /*Keep reference of current stack pointer*/
        stack_arg_arr[arg_count] = (char *) *stackptr;

        /*Make sure the length is properly aligned*/
        len_stack = (len_stack%4 != 0) ? 4*(len_stack/4)+ 4: len;
        *stackptr += len_stack;
        arg_count +=1;
    }
    return err;
}
/************************************************************************************************************************************************************************/
