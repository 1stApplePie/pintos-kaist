#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* project 2 */
struct exit_info {
    tid_t parent_tid;
    tid_t child_tid;

    int exit_status;
    struct list_elem exit_elem;
    struct semaphore exit_sema;
};

/* project 3 */
struct file_info {
    struct file *file;
    off_t ofs;
    uint32_t read_bytes;
    bool writable;
};

struct thread * find_child_process(tid_t);

#endif /* userprog/process.h */
