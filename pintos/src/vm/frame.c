#include "vm/frame.h"

struct lock frame_table_lock;
struct list frame_table;

void *frame_alloc_evict (enum palloc_flags);
void frame_init (struct frame *f, struct pte_elem *elem);

void 
frame_alloc_init (void) {
  lock_init (&frame_table_lock);
  list_init (&frame_table);
}

void*
frame_alloc_get_page (enum palloc_flags flags, struct pte_elem *elem) {
  if (!flags & PAL_USER)
    return NULL;
  struct frame *f = (struct frame *)malloc (sizeof (struct frame));
  f->frame_ptr = palloc_get_page (flags);
  if (!f->frame_ptr)
    f->frame_ptr = frame_alloc_evict (flags);
  if (!f->frame_ptr)
      PANIC ("No enough swap space to evict frame.");
  frame_init (f, elem);
  return f->frame_ptr;
}

void 
frame_free_page (void *page) {
  struct list_elem *e;
  lock_acquire (&frame_table_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e)) {
    struct frame *f = list_entry (e, struct frame, elem);
    if (f->frame_ptr == page) {
      list_remove (e);
      palloc_free_page (page);
      free (f);
    }
  }
  lock_release (&frame_table_lock);
}

void
frame_init (struct frame *f, struct pte_elem *e) {
  f->pte_elem = e;
  f->thread_belonged = thread_current ();
  lock_acquire (&frame_table_lock);
  list_push_back (&frame_table, &f->elem);
  lock_release (&frame_table_lock);
}

void*
frame_alloc_evict (enum palloc_flags flags) {
  lock_acquire (&frame_table_lock);
  //TODO: Implement swap
  lock_release (&frame_table_lock);
  return NULL;
}
