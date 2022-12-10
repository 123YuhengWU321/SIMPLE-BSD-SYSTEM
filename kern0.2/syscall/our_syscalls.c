#include <types.h>
#include <uio.h>
#include <lib.h>
#include <lib.h>
#include <endian.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <syscall.h>
#include <open_file.h>
#include <filetable.h>
#include <proc.h>
#include <limits.h>

/**
 * open opens the file, device, or other kernel object named by the pathname filename. 
 * The flags argument specifies how to open the file. open returns a file handle suitable 
 * for passing to read, write, close, etc. This file handle must be greater than or equal to zero. 
 * 
 * Return: On success, open returns a nonnegative file handle.
 * On error, -1 is returned, and errno is set according to the error encountered.
 */
int
sys_open(const_userptr_t user_filepath, int flags, mode_t mode, int *result_fd)
{   
    if(user_filepath == NULL){
        return EFAULT;
    }

    int   result;
    bool  flags_is_valid;
    char  *kernel_filepath;
    struct open_file *of;
    struct filetable *ft = curproc->p_filetable;

   
    flags_is_valid = open_flag_doIhold(flags);
    /*Error for invalid flag*/
    if(!flags_is_valid)
        return EINVAL;

   
    kernel_filepath = kmalloc(PATH_MAX);
    /*Error for malloc failiure*/
    if(kernel_filepath == NULL){
        return ENOMEM;
    }

    /*Use copyin to copy data from user space to kernel space*/
    result = copyinstr(user_filepath, kernel_filepath, PATH_MAX, NULL);
    if(result){
        goto err;
    }

    /*Open the file in kenrnel path, using the open files table*/
    result = open_file_open(kernel_filepath, flags, mode, &of); 
    if(result){
        goto err;
    }
    
    kfree(kernel_filepath);

    if(ft != NULL){
        /*Place the open entry info inside the current process's filetable*/
        result = ft_insert(ft, of, result_fd); 
        if(result){ 
          /*On error inserting, decrease the of refcount*/
           goto ft_insert_err;
        }
    }
    return 0;

err:
    kfree(kernel_filepath);
    return result;

ft_insert_err:
    decrease_refount(of);
    return result;
}

/**
 * When sys_close is invokerd, the file handle fd is closed. 
 * The same file handle may then be returned again from open, dup2, pipe, 
 * or similar calls. Other file handles are not affected in any way, even if they are attached to the same file.
 * 
 * Return: On success, close returns 0. On error, -1 is returned, and errno is set according to the error encountered.
 */
int
sys_close(int fd)
{
    /*fd is not a valid file handle*/
    if(fd < 0 || fd >= OPEN_MAX){
        return EBADF;
    }
    
    struct filetable *ft = curproc->p_filetable;

    if(ft == NULL){
        return EFAULT;
    }
    
    /**
     * Set the current filetable entry to null but still need 
     * to save the information for the openentry as this should
     * not be completly removed unless ref count is less than
     * one
     */
    struct open_file *of = ft->entries[fd];
    if(of == NULL){
		 return EBADF;
	}

	ft->entries[fd] = NULL;

    /*Decrement the open_file table entry reference*/
    decrease_refount(of);

    return 0;
}


/**
 * read reads up to buflen bytes from the file specified by fd, at the location in the file specified by the current seek position of the file, 
 * and stores them in the space pointed to by buf. The file must be open for reading.
 * The current seek position of the file is advanced by the number of bytes read.
 * 
 * Each read (or write) operation is atomic relative to other I/O to the same file. 
 * Note that the kernel is not obliged to (and generally cannot) make the read atomic 
 * with respect to other threads in the same process accessing the I/O buffer during the read.
 * 
 * Return:
 * The count of bytes read is returned. This count should be positive. A return value of 0 should be construed as signifying end-of-file.
 * On error, read returns -1 and sets errno to a suitable error code for the error condition encountered.
 */
int
sys_read(int fd, userptr_t buf, size_t buflen,int *retval)
{
    if(buf == NULL){
        return EFAULT;
    }
    int    result;
    struct open_file   *of;

    struct iovec        iov_read;
    struct uio          uio_read;
   
    if(fd<0 || fd>= OPEN_MAX){
        return EBADF;
    } 

    if(buf == NULL || buflen<1){
        return EFAULT;
    }

    result = ft_get(curproc->p_filetable,fd,&of);
    if(result)
        goto err;
    
    /*Using the file_offset_lk to make sure the read is atomic*/
    lock_acquire(of->file_offset_lk);
    if(of == NULL || of->access_mode == O_WRONLY){
        lock_release(of->file_offset_lk);
        return EBADF;
    }

    /*Initlize the UIO for read*/
    iov_read.iov_ubase = (userptr_t)buf;
    iov_read.iov_len = buflen;
    uio_read.uio_iov = &iov_read;
    uio_read.uio_iovcnt = 1;
    uio_read.uio_resid = buflen;
    uio_read.uio_offset = of->file_offset;
    uio_read.uio_segflg = UIO_USERSPACE;
    uio_read.uio_rw = UIO_READ;
    uio_read.uio_space = curproc->p_addrspace;

    /*Do the actual read using VOP_READ, on error release the lock*/
    result = VOP_READ(of->vn, &uio_read);
    if(result){
        lock_release(of->file_offset_lk);
        goto err;
    }

    /*Get amount that are already read*/
    size_t buflen_tmp = buflen - uio_read.uio_resid;
    /*Updating the open entry offset*/
    of->file_offset = uio_read.uio_offset;
    lock_release(of->file_offset_lk);

    /*Store the amount read as a reference*/
    *retval = buflen_tmp;

    return 0;

err:
    return result;
}

/**
 *write writes up to buflen bytes to the file specified by fd, 
 *at the location in the file specified by the current seek position of the file, taking the data from the space pointed to by buf. The file must be open for writing.
 *The current seek position of the file is advanced by the number of bytes written.
 *each write (or read) operation is atomic relative to other I/O to the same file. 
 * The kernel is not obliged to (and generally cannot) make the write atomic with respect to other 
 * threads in the same process accessing the I/O buffer during the write. 
 *
 * Return:
 * The count of bytes written is returned. This count should be positive. 
 * A return value of 0 means that nothing could be written, but that no error occurred; 
 * this only occurs at end-of-file on fixed-size objects. On error, write returns -1 and 
 * sets errno to a suitable error code for the error condition encountered.
 * 
 * Implementation logic is the same as read
 */
int
sys_write(int fd,  userptr_t buf, size_t nbytes,int *retval)
{
    if(buf == NULL){
        return EFAULT;
    }

    int    result;
    struct uio          uio_write;

    struct open_file   *of;
    struct iovec        iov_write;

    if(fd<0 || fd>= OPEN_MAX){
        return EBADF;
    } 

    if(buf == NULL || nbytes<1){
        return EFAULT;
    }

    result = ft_get(curproc->p_filetable,fd, &of);
    if(result)
        goto err;

    lock_acquire(of->file_offset_lk);
    if(of == NULL || of->access_mode == O_RDONLY){
        lock_release(of->file_offset_lk);
        return EBADF;
    }

    /*Initlize the UIO for write*/
    iov_write.iov_ubase = (userptr_t)buf;
    iov_write.iov_len = nbytes;
    uio_write.uio_iov = &iov_write;
    uio_write.uio_iovcnt = 1;
    uio_write.uio_resid = nbytes;
    uio_write.uio_offset = of->file_offset;
    uio_write.uio_segflg = UIO_USERSPACE;
    uio_write.uio_rw = UIO_WRITE;
    uio_write.uio_space = curproc->p_addrspace;

    result = VOP_WRITE(of->vn, &uio_write);
    if(result){
        lock_release(of->file_offset_lk);
        goto err;
    }

    /*Get amount that are already written*/
    size_t buflen_tmp = nbytes - uio_write.uio_resid;

    of->file_offset = uio_write.uio_offset;
    lock_release(of->file_offset_lk);

    *retval = buflen_tmp;

    return 0;

err:
    return result;
}

/**
change current position in file
lseek alters the current seek position of the file handle filehandle, seeking to a new position based on pos and whence.
If whence is
    SEEK_SET, the new position is pos.
    SEEK_CUR, the new position is the current position plus pos.
    SEEK_END, the new position is the position of end-of-file plus pos.
    anything else, lseek fails.

return on success: 0 (function); result off_t (off_t *ret)
return on error: 	
	EBADF	fd is not a valid file handle.
    ESPIPE	fd refers to an object which does not support seeking.
    EINVAL	whence is invalid.
    EINVAL	The resulting seek position would be negative.
*/
int
sys_lseek(int fd, uint32_t first, uint32_t second, int whence, uint32_t *first_ret, uint32_t *second_ret){
    
    uint64_t offset;
    join32to64(first, second, &offset);

    struct filetable *ft = curproc->p_filetable;
    
    if(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END){
        return EINVAL;
    }

    if(fd < 0 || fd >= OPEN_MAX){
        return EBADF;
    }

    struct open_file *of = ft->entries[fd];
    if(of == NULL){
        return EBADF;
    }

    if (!VOP_ISSEEKABLE(of->vn)) {
		return ESPIPE;
	}

    lock_acquire(of->file_offset_lk);
    
    off_t pos;
    
    if(whence == SEEK_SET){
        pos = offset;
    }else if(whence == SEEK_CUR){
        pos = offset + of->file_offset;
    }else if(whence == SEEK_END){
        struct stat *st = kmalloc(sizeof(struct stat));
        if(st == NULL){
            lock_release(of->file_offset_lk);
            return ENOMEM;
        }
        int error = VOP_STAT(of->vn, st);
		if (error) {
			lock_release(of->file_offset_lk);
			return error;
		}
        pos = st->st_size + offset;
        kfree(st);
    }
    
    if (pos < 0) {
		lock_release(of->file_offset_lk);
		return EINVAL;
	}
    of->file_offset = pos;
   
    lock_release(of->file_offset_lk);

    split64to32(pos, first_ret, second_ret);

    return 0;
}

/**
dup2 - clone file handles

return on success:  0 (function); newfd (stored in int *ret)
return on error: 	
	EBADF	oldfd is not a valid file handle, or newfd is a value that cannot be a valid file handle.
    EMFILE	The process's file table was full, or a process-specific limit on open files was reached.
    ENFILE	The system's file table was full, if such a thing is possible, or a global limit on open files was reached.
*/
int
sys_dup2(int oldfd, int newfd, int *ret)
{
   if(newfd >= OPEN_MAX || newfd < 0 || oldfd >= OPEN_MAX || oldfd < 0){
		return EBADF;
	}

    if (oldfd == newfd) {
		*ret = newfd;
		return 0;
	}

    struct filetable *ft = curproc->p_filetable;

    struct open_file *of_old = ft->entries[oldfd];

    if(of_old == NULL){
        return EBADF;
    }

    //keep the open_file at ft->entries[newfd], and copy content
    //at entries[oldfd] to entries[newfd]
	struct open_file *of_new = ft->entries[newfd];
	
    if(of_new != NULL){
        decrease_refount(of_new);
    }

    ft->entries[newfd] = of_old;
    increase_refount(of_old);
    
    *ret = newfd;
    return 0;
}

/*
change current directory
The current directory of the current process is set to the directory named by pathname.

return on success: 0
return on error: 	
    ENODEV	The device prefix of pathname did not exist.
    ENOTDIR	A non-final component of pathname was not a directory.
    ENOTDIR	pathname did not refer to a directory.
    ENOENT	pathname did not exist.
    EIO	A hard I/O error occurred.
    EFAULT	pathname was an invalid pointer.
*/
int
sys_chdir(const_userptr_t path)
{   
    if(path == NULL){
        return EFAULT;
    }

	char* buf = kmalloc(PATH_MAX);

	if (buf == NULL) {
		goto err_ENOMEM;
	}

    size_t *size = kmalloc(sizeof(int));
	if (buf == NULL) {
		goto err_ENOMEM;
	}

    int error;
    
    error = copyinstr((const_userptr_t) path, buf, PATH_MAX, size);
    if(error){
        goto err;
    }

    error = vfs_chdir(buf);
    if(error){
        goto err;
    }
    
    return 0;

err_ENOMEM:
    return ENOMEM;

err:
    kfree(buf);
    kfree(size);
    return error;

}

/**
get name of current working directory (backend)

return on success: 0
return on error: 	
	ENOENT	A component of the pathname no longer exists.
    EIO	A hard I/O error occurred.
    EFAULT	buf points to an invalid address.
*/
int
sys___getcwd(userptr_t buf, size_t buflen, int *ret)
{   
    if(buf == NULL){
        return EFAULT;
    }

    struct iovec i;
	struct uio u;
    
    /*Initlize the uio*/
    i.iov_kbase = (userptr_t)buf;
    i.iov_len = buflen;

    u.uio_iov = &i;
    u.uio_iovcnt = 1;
    u.uio_offset = 0;
    u.uio_resid = buflen;
    u.uio_rw = UIO_READ;
    u.uio_segflg = UIO_USERSPACE;
    u.uio_space = curproc->p_addrspace;

    int error_vfs_getcwd = vfs_getcwd(&u);
	if (error_vfs_getcwd) {
		return error_vfs_getcwd;
	}

    *ret = buflen - u.uio_resid;

    return 0 ;
}