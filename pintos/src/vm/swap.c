#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/block.h"

#include <bitmap.h>

static struct block *swap_space;
static struct lock swap_lock;
static struct bitmap *used_slot_map; /* Our slot size is PGSIZE */
static const bool SWAP_USED = true;
static const size_t sectors_in_page = PGSIZE / BLOCK_SECTOR_SIZE; //# of sectors in a page

void
swap_init (void) {
  swap_space = block_get_role (BLOCK_SWAP);
  lock_init (&swap_lock);
  used_slot_map = bitmap_create (block_size (swap_space) / sectors_in_page);
  bitmap_set_all (used_slot_map, !SWAP_USED);
}

swap_id_t
swap_write_page (void *kpage) {
  lock_acquire (&swap_lock);
  swap_id_t slot_begin = bitmap_scan_and_flip (used_slot_map, 0, 1, !SWAP_USED);
  uint32_t sector_begin = slot_begin * sectors_in_page;
  size_t i;
  uint8_t *kaddr = (uint8_t *)kpage;

  if (slot_begin == BITMAP_ERROR)
    return SWAP_ID_ERROR;
  /* Write one sector each time */
  for (i = 0; i < sectors_in_page;i++, kaddr += BLOCK_SECTOR_SIZE) //move a sector a time
    block_write (swap_space, sector_begin+i, kaddr);
  lock_release (&swap_lock);
  return slot_begin;
}

void
swap_read_page (swap_id_t id, void *uvaddr) {
  size_t i;
  size_t sector_begin = id * sectors_in_page;
  uint8_t *uaddr = (uint8_t *)uvaddr;
  lock_acquire (&swap_lock);
  if (bitmap_test (used_slot_map, id) == SWAP_USED) { //if this slot has data
    bitmap_flip (used_slot_map, id);
    for (i = 0; i < sectors_in_page; i++, uaddr += BLOCK_SECTOR_SIZE)
      block_read (swap_space, sector_begin+i, uaddr);
  }
  lock_release (&swap_lock);
}
