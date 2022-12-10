#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <open_file.h>
#include <kern/unistd.h>
#include <synch.h>
#include <vnode.h>
#include <spinlock.h>
#include <vnode.h>
#include <vfs.h>
#include <lib.h>

/*Valid flags for open'w read and write operations*/
volatile int rdwr_flags = O_RDONLY | O_WRONLY | O_RDWR;
/*Desctuctor instatntiation*/
void open_file_destroy(struct open_file *file); 

//function(s) are adapted from vnode.c and vfs.c

/*Constructor*/
static struct open_file*
open_file_create(struct vnode *vn_ptr, int openflag, off_t offset)
{
    struct open_file *of;

    of = (struct open_file *) kmalloc(sizeof(struct open_file));   
    if(of == NULL){
        return NULL;
    }

	/**
	 *Struct initilization, note that openflag is bit
	 * wise and with the valid flags and 
	 * ref count is set to one which is important in whether we should
	 * destroy the entry or just hide names of entry when necessary
	 */
    of->access_mode = openflag & rdwr_flags;
    of->vn = vn_ptr;

	of->file_offset_lk = lock_create("of_offsetlock");
	if(of->file_offset_lk == NULL){
		kfree(of);
		return NULL;
	}
    of->file_offset = offset;
    
	spinlock_init(&of->file_refcount_lk);
	if(of->file_offset_lk == NULL){
		lock_destroy(of->file_offset_lk);
		kfree(of);
		return NULL;
	}
	of->file_refcount = 1;

    return of;       
}

/*Desctructor*/
void
open_file_destroy(struct open_file *file)
{
	/**
	 * This should be called when there are no refcount to 
	 * a specific open entry, in this case it should actually be closed
	 * and all of its attributes destroyed or else the open entry info should
	 * still be saved as a reference
	 */
	vfs_close(file->vn);
	lock_destroy(file->file_offset_lk);
	spinlock_cleanup(&file->file_refcount_lk);
	kfree(file);
}

/*Actual function for oepn file entries, open the file with a speific vnode and save the info into openfile entry*/
int
open_file_open(char *filename, int openflags, mode_t mode,
	           struct open_file **retfd)
{
	int result;
	struct vnode *vn;
	struct open_file *file;

	/*Open the file using vnode and get vnode info on success*/
	result = vfs_open(filename, openflags, mode, &vn);
	if (result) {
		goto vfsopen_err;
	}

	/**
	 * After suceessfully opening with vnode, save all open enrty
	 * info with that vnode into the open entry table, use the 
	 * retfd pointer to reference the information for other 
	 * function's uses
	 */
	file = open_file_create(vn, openflags & rdwr_flags, 0);
	if (file == NULL) {
		goto open_entry_err;
	}
	
	/*Keep the file as a reference for later use*/
    *retfd = file;

	return 0;

vfsopen_err:
	return result;

open_entry_err:
	vfs_close(vn);
	return ENOMEM;
}

/*Chekcker method for determing whether the opneflag is valid for open syscall*/
bool
open_flag_doIhold(int openflag)
{
     int all_valid_openflags = O_RDONLY | O_WRONLY | O_RDWR |
                               O_CREAT  | O_EXCL   | O_TRUNC|
                               O_APPEND;
     bool result = ((openflag & all_valid_openflags) == openflag) ? true:false;

     return result;                   
}