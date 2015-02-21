#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stdint.h>
#include "vm/swap.h"

#define UPG_EVICTABLE 0x01 /* Used in Clock Algorithm */
#define UPG_ON_MMAP 0x02 /* The page is from mmap file */
#define UPG_ON_SWAP 0x04 /* The page is from Swap space */
#define UPG_INVALID 0x08 /* Current page has not been map */
#define UPG_WRITABLE 0x10 /* READ WRITE */

struct page {
  void *uvaddr;
  struct file *fptr;
  size_t frs; /* File read size */
  size_t fs; /* File start */
  size_t fzs; /* File zero size */
  uint8_t flags; /* Flag bits use to record some information */
  swap_id_t swap_id; /* Swap id used in swap.c */
  struct hash_elem elem;
};

void page_table_init (struct hash *table);
void page_table_destory (struct hash *table);
/* Insert a copy of UVPAGE into the page TABLE. */
bool page_table_insert (struct hash *table, struct page *uvpage);

struct page *page_get_page (void *uvaddr);
bool page_map_page (struct page *uvpage);

#define STACK_OVER_FLOW 8 * 1024 * 1024 //8 MB Stack size

bool page_user_stack (void *uvaddr);

#endif
