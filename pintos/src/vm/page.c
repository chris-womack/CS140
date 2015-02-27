#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"

#include <string.h>

static unsigned 
page_hash (const struct hash_elem *e, void *aux UNUSED) {
  struct page *uvpage = hash_entry (e, struct page, elem);
  return hash_int ((int)uvpage->uvaddr);
}

static bool 
page_compare (const struct hash_elem *a, const struct hash_elem *b, void*aux UNUSED) {
  struct page *ap = hash_entry (a, struct page, elem);
  struct page *bp = hash_entry (b, struct page, elem);
  return ap->uvaddr < bp->uvaddr;
}

static void
page_free (struct hash_elem *e, void *aux UNUSED) {
  struct page *uvpage = hash_entry (e, struct page, elem);
  struct thread *cur = thread_current ();
  if (!(uvpage->flags & UPG_INVALID)) {
    void *kpage = pagedir_get_page (cur->pagedir, uvpage->uvaddr);
    pagedir_clear_page (cur->pagedir, uvpage->uvaddr);
    frame_free_page (kpage);
  }
  free (uvpage);
}

void 
page_table_init (struct hash *table) {
  hash_init (table, page_hash, page_compare, NULL);
}

void 
page_table_destory (struct hash *table) {
  hash_destroy (table, page_free);
}

struct page*
page_get_page (void *uvaddr) {
  struct page p;
  p.uvaddr = pg_round_down (uvaddr);
  struct hash_elem *e = hash_find (&thread_current ()->page_table, &p.elem);
  return e ? hash_entry (e, struct page, elem) : NULL;
}

/* Map page helper */
static bool page_map_swap (struct page *uvpage);
static bool page_map_file (struct page *uvpage);

bool 
page_map_page (struct page *uvpage) {
  uvpage->flags &= ~UPG_EVICTABLE;
  if (uvpage->flags & UPG_INVALID) {
    if (uvpage->flags & UPG_ON_SWAP)
      return page_map_swap (uvpage);
    else
      return page_map_file (uvpage);
  }
  return false;
}

bool
page_map_swap (struct page *uvpage) {
  void *kpage = frame_alloc_get_page (PAL_USER, uvpage);
  if (kpage) {
    if (install_page (uvpage->uvaddr, kpage, uvpage->flags & UPG_WRITABLE)) {
      swap_read_page (uvpage->swap_id, uvpage->uvaddr);
      uvpage->flags &= ~UPG_INVALID;
    } else 
      frame_free_page (kpage);
  }
  return uvpage->flags & ~UPG_INVALID;
}

bool
page_map_file (struct page *uvpage) {
  void *kpage = NULL;
  size_t read_bytes = 0;
  kpage = frame_alloc_get_page (uvpage->frs == 0 ? PAL_USER|PAL_ZERO : PAL_USER, uvpage);
  if (kpage) {
    if (uvpage->frs > 0) {
      lock_acquire (&fs_lock);
      read_bytes = file_read_at (uvpage->fptr, kpage, uvpage->frs, uvpage->fs);
      lock_release (&fs_lock);
      if (read_bytes == uvpage->frs) {
        memset (kpage+read_bytes, 0, uvpage->fzs);
      } else {
        frame_free_page (kpage);
        return false;
      }
    }
    if (install_page (uvpage->uvaddr, kpage, uvpage->flags & UPG_WRITABLE)) {
      uvpage->flags &= ~UPG_INVALID;
      return true;
    } else {
      frame_free_page (kpage);
      return false;
    }
  }
  return false;
}

bool
page_table_insert (struct hash *table, struct page *uvpage) {
  struct page *p = (struct page*)malloc (sizeof (struct page));
  if (p) {
    *p = *uvpage;
    p->flags = UPG_EVICTABLE 
      | (uvpage->flags & UPG_ON_MMAP)
      | (uvpage->flags & UPG_ON_SWAP)
      | UPG_INVALID
      | (uvpage->flags & UPG_WRITABLE);
    if (process_mmap_file (p))
      return hash_insert (&thread_current ()->page_table, &p->elem);
  }
  return false;
}

bool
page_user_stack (void *uvaddr) {
  if ((size_t)(PHYS_BASE-uvaddr) > STACK_OVER_FLOW)
    return false;

  struct page *p = (struct page*)malloc (sizeof (struct page));
  void *kpage = NULL;
  struct thread *cur = thread_current ();
  if (p) {
    p->uvaddr = pg_round_down (uvaddr);
    p->flags = (intr_context () ? UPG_EVICTABLE : 0) | UPG_ON_SWAP | UPG_WRITABLE;
    kpage = frame_alloc_get_page (PAL_USER, p);
    if (kpage) {
      if (install_page (p->uvaddr, kpage, true))
        return (hash_insert (&cur->page_table, &p->elem) == NULL);
      else {
        frame_free_page (kpage);
        free (p);
      }
    } else
      free (p);
  }
  return false;
}
