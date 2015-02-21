#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"
#include "vm/page.h"

/*
  Each entry in the frame table contains a pointer to the page, if any,
  that currently occupies it. The Diagram of our frame structure is as follow:
*/
struct frame {
  void *kpage; /* Kernel page */
  struct thread *used_process; /* Current process that uses it */
  struct page *uvpage; /* Current user virtual page that occupies it */
  struct list_elem elem; /* Elem of frame table. */
};

/* Init the frame table. */
void frame_alloc_init (void);
/* Get a frame to map the address of UVPAGE. */
void *frame_alloc_get_page (enum palloc_flags flags, struct page *uvpage);
/* Free the frame and the map of user virtual page will be invaild. */
void frame_free_page (void *page);

#endif
