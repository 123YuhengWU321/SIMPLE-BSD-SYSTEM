#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <limits.h> 

/*each process has its own filetable and contains info about the open entries*/
struct filetable {
	struct open_file *entries[OPEN_MAX];
};

/*Constructor and destructor*/
struct filetable *ft_create(void);
void ft_destroy(struct filetable *ft);

/*Getter for filetable*/
int ft_get(struct filetable *ft, int fd, struct open_file **ret);

/*Putter for filetable*/
int ft_insert(struct filetable *ft, struct open_file *file, int *fd);

/*counter methods for open entries in filetable, similar logic as Vnode's incref/decrecf*/
void increase_refount(struct open_file *file);
void decrease_refount(struct open_file *file);



#endif 
