#ifndef _OPENFILE_H_
#define _OPENFILE_H_

#include <spinlock.h>

/**
 * This is the same structure from the open files table and vnode.h
 * https://sites.google.com/view/cpen331fall2020/calendar/file-descriptors-in-unix
 * each open file table entry is assigned with a status, an offset and a vnode
 * 
 * The struct of openfile is designed in the same way as a vnode where it has a ref
 * count and protected by a spinlock
 */

struct open_file {
        int access_mode;                  /*The open flag*/    

        struct vnode *vn;                 /*Its counterpart at the vnode, used in vfs_open*/

        volatile int file_refcount;       /*Refcount needed such that file is only hidden if there are at least one ref*/
        struct spinlock file_refcount_lk;

        off_t  file_offset;               /*The file offset and its lock*/
        struct lock *file_offset_lk;
};

/*Destrcutor for the open file entry, constructor is in open_file.c*/
void open_file_destroy(struct open_file *file);

/*flag checker for all flags*/
bool
open_flag_doIhold(int openflag);

/*Helper for doing the actual sys_open*/
int open_file_open(char *filename, int openflags, mode_t mode, struct open_file **ret);
#endif
