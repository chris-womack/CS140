#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"

struct pte_elem {
  uint32_t pte; /* Page table entry pont to the current page. */
  struct list_elem elem; /* Elem of pte_list. */
};

/*
  Each entry in the frame table contains a pointer to the page, if any,
  that currently occupies it. The Diagram of our frame structure is as follow:
*/
struct frame {
  void *frame_ptr;
  struct thread *thread_belonged;
  struct pte_elem *pte_elem;
  struct list_elem elem; /* Elem of frame table. */
};

void frame_alloc_init (void);
void *frame_alloc_get_page (enum palloc_flags flags, struct pte_elem *elem);
void frame_free_page (void *page);

#endif
