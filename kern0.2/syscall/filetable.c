#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <open_file.h>
#include <filetable.h>

/*open file entry counter instntiation*/
void increase_refount(struct open_file *file);
void decrease_refount(struct open_file *file);

/*
create a filetable
return on success:	the created filetable
return on error: 	NULL (caused by ENOMEM (Out of memory))
*/
struct filetable *ft_create(){
    struct filetable *ft;

    ft = kmalloc(sizeof(struct filetable));
    if(ft == NULL){
        return NULL;
    }    
    
	for (int i = 0; i < OPEN_MAX; i++) {
        ft->entries[i] = NULL;
    }
    return ft;
}

/*
destory this filetable
decrease reference count for every non-Null entry in this filetable
return on success:	NONE
return on error: 	NONE
*/
void ft_destroy(struct filetable *ft){
	for (int i = 0; i < OPEN_MAX; i++) {
		//start from zero because we do want to destory stdin/out/err
		if (ft->entries[i] != NULL) {
			//	we shuld decrease reference count
			decrease_refount(ft->entries[i]);
			ft->entries[i] = NULL;
		}
	}
	kfree(ft);
}

/*
try to get ft[file_descriptor], then set ret to ft[file_descriptor] if not null
return on success:	0
return on error: 	EBADF (Bad file number)
*/
int ft_get(struct filetable *ft, int fd, struct open_file **ret)
{	
	if(fd >= OPEN_MAX ){
		return EBADF;
	}

	if(fd < 0){
		return EBADF;
	}

	struct open_file *file = ft->entries[fd];
	
	if (file == NULL) {
		return EBADF;
	}

	/*Saving the open file entry as a reference*/
	*ret = file;

	return 0;
}

/*
try to insert a open_file to filetable, if filetable is not full
return on success:	NONE
return on error: 	EMFILE (Too many open files)
*/
int ft_insert(struct filetable *ft, struct open_file *of, int *fd){
	KASSERT(ft != NULL);
	//start from index 3 because 0,1,2 are stdin stdout stderr, they are always there
    for (int i = 3; i < OPEN_MAX; i++) {
		if (ft->entries[i] == NULL) {
			ft->entries[i] = of;
			*fd = i;
			return 0;
		}
	}
	return EMFILE;
}

/*
 Increase  the reference of do_sys_open
 similiar idea as vnode_incref/decref
*/
void 
increase_refount(struct open_file *file_open)
{
    spinlock_acquire(&file_open->file_refcount_lk);
    file_open->file_refcount++;
    spinlock_release(&file_open->file_refcount_lk);
}

/*
 decrease  the reference of do_sys_open
 similiar idea as vnode_incref/decref
*/
void 
decrease_refount(struct open_file *file_open)
{
    if(file_open == NULL){
        return;
    }

    spinlock_acquire(&file_open->file_refcount_lk);

    int case_num = (file_open->file_refcount > 1) ? 1 : 2;

    switch (case_num)
    {
    	case 1:
            file_open->file_refcount--;
            spinlock_release(&file_open->file_refcount_lk);
            break;
		/**In the case where there are no ref count or error we must free up all memory
		   here is when we can actually destroy the open file entry
		*/
        case 2:
            spinlock_release(&file_open->file_refcount_lk);
            open_file_destroy(file_open);
            break;

        default:
            break;
    }
}