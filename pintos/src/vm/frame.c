#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/malloc.h"
static struct lock frame_table_lock;
static struct list frame_table;

void *frame_alloc_evict (enum palloc_flags flags);

void 
frame_alloc_init (void) {
  lock_init (&frame_table_lock);
  list_init (&frame_table);
}

void*
frame_alloc_get_page (enum palloc_flags flags, struct page *uvpage) {
  if (!(flags & PAL_USER)) //these frame should be used ONLY in user prog
    return NULL;
  void *kpage = palloc_get_page (flags);
  if (kpage == NULL) {
    /* else evict a frame from frame table */
    kpage = frame_alloc_evict (flags);
    if (kpage == NULL)
      PANIC ("Not enough spaces for allocating a free frame.");
  }
   /* Add a frame to frame table */
  struct frame *f = (struct frame *)malloc (sizeof (struct frame));
  f->kpage = kpage;
  f->used_process = thread_current ();
  f->uvpage = uvpage;
  lock_acquire (&frame_table_lock);
  list_push_back (&frame_table, &f->elem);
  lock_release (&frame_table_lock);
  return f->kpage;
}

void 
frame_free_page (void *kpage) {
  struct list_elem *e;
  /* Scan the frame table to find an elem for KPAGE */
  lock_acquire (&frame_table_lock);
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e)) {
    struct frame *f = list_entry (e, struct frame, elem);
    if (f->kpage == kpage) {
      list_remove (e); //remove elem from the table
      palloc_free_page (kpage); //free the page to user pool
      free (f); //free the block memory
      break;
    }
  }
  lock_release (&frame_table_lock);
}

/* Free a frame in frame table. Write back to Swap Space or File SYS corresponding to the 
   UVPAGE of that frame. (Clock Algorithm Implementation)*/
void*
frame_alloc_evict (enum palloc_flags flags) {
  lock_acquire (&frame_table_lock);
  struct list_elem *e = list_begin (&frame_table);
  struct frame *f;
  while (true) { /* Scan the frame table to find a BIT_R is zero */
    f = list_entry (e, struct frame, elem);
    if (f->uvpage->flags & UPG_EVICTABLE) {
      /* If current page has been access, it need to update access bit */
      if (pagedir_is_accessed (f->used_process->pagedir, f->uvpage->uvaddr)) {
        pagedir_set_accessed (f->used_process->pagedir, f->uvpage->uvaddr, false);
        goto check_next;
      }
      
      /* If current page is dirty, write to swap space or filesys before evict */
      else if (pagedir_is_dirty (f->used_process->pagedir, f->uvpage->uvaddr))
        if (f->uvpage->flags & UPG_ON_MMAP) { /* If it comes from a map file */
          /* write it back to filesys */
          lock_acquire (&fs_lock);
          file_write_at (f->uvpage->fptr, f->kpage, f->uvpage->frs, f->uvpage->fs);
          lock_release (&fs_lock);
        } else { /* Swap to swap space */
          f->uvpage->flags &= UPG_ON_SWAP;
          if ((f->uvpage->swap_id = swap_write_page (f->kpage)) == SWAP_ID_ERROR)
            return NULL;
        }

      /* If current page is from Swap Space, write back */
      else if (f->uvpage->flags & UPG_ON_SWAP)
        if ((f->uvpage->swap_id = swap_write_page (f->kpage)) == SWAP_ID_ERROR)
          return NULL;
      
      /* Free the frame and remove it from frame table.
         After this step, any attemp to access the UVPAGE 
         of the frame will cause a page fault. */
      f->uvpage->flags &= UPG_INVALID;
      list_remove (&f->elem);
      pagedir_clear_page (f->used_process->pagedir, f->uvpage->uvaddr);
      palloc_free_page (f->kpage);
      free (f);
      break;
    }
  check_next:
    e = list_next (e) == list_end (&frame_table)
      ? list_begin (&frame_table)
      : list_next (e);
  }
  
  lock_release (&frame_table_lock);
  return palloc_get_page (flags);
}
