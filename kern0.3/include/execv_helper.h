#ifndef _EXECVHELPER_H_
#define _EXECVHELPER_H_

#include <limits.h>

int check_arg_length(char **args, int* argc);

int copyin_arg(int *bufsize, int argc, char **args, char **arg_ptr, char **arg_buf, char *tmp_arg_dest);

int copyout_arg(vaddr_t *stackptr, int argc, char **stack_arg_arr);

int update_stack_pointer(vaddr_t *stackptr, int argc, int len, char **arg_buf, char **stack_arg_arr);

#endif /* _EXECVHELPER_H_ */
