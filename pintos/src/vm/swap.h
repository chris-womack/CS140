#ifndef VM_SWAP_H
#define VM_SWAP_H
#include <bitmap.h>

typedef uint32_t swap_id_t;
#define SWAP_ID_ERROR BITMAP_ERROR

void swap_init (void);
swap_id_t swap_write_page (void *kpage);
void swap_read_page (swap_id_t id, void *kpage);

#endif
