#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#ifdef VM
#include "vm/page.h"
#endif

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
/* Add a file to the list of files process opens */
int process_add_openfile (struct file *fptr);
bool install_page (void *upage, void *kpage, bool writable);

#ifdef VM
typedef int mmapid_t;
struct mmap_file {
  struct page *map_page;
  mmapid_t id;
  struct list_elem elem;
};

bool process_mmap_file (struct page *uvpage);

#endif

#endif /* userprog/process.h */
