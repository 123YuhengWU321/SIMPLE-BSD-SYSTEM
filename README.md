# SIMPLE-BSD-SYSTEM
Teaching based BSD design, the system used is OS161 </br>
Only included the kernel file as reference, please do not copy the implementation if you are currently working with OS161, for your own benefit</br>

## Highlight1 - Synchronization
Muti_threaded algorithm at kern0.1, important file which contains the implementation of the alogorithm is placed at kern0.1/synchprobs/airballoon.c</br>
The detailed explanation for the algorithm is at https://sites.google.com/view/cpen331fall2022/assignments/assignment-3?authuser=0</br>

## Highlight2 - File Syscalls and Filetable Design
Filetable design is placed at kern0.2/syscall, openfile.c, filetable.c are for our filetable design and our_syscalls.c has the file related syscalls</br>
Refer to https://sites.google.com/view/cpen331fall2022/assignments/assignment-4?authuser=0 for detailed explanantions </br>
Refer to https://people.ece.ubc.ca/~os161/man/ for what each syscall does </br>

## Highlight3 - Process Related Syscalls and Process Design
Process related design is at kern0.3/proc/proc.c, process realted syscall is placed at kern0.3/syscall/A5_syscalls.c and kern0.3/syscall/execv_helper.c</br>
Refer to https://sites.google.com/view/cpen331fall2022/assignments/assignment-5?authuser=0 for detailed explanantions </br>
Refer to https://people.ece.ubc.ca/~os161/man/ for what each syscall does </br>

## Highlight3 - Paging and Memory Management:
Will be added soon
