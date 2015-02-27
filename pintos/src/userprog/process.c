#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static void push (void **esp, void *data, size_t length);
extern const int PROCESS_MAGIC;

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy, *fn_mutable, *save_ptr, *exec_name;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_mutable = palloc_get_page (0);
  if (fn_mutable == NULL)
    return TID_ERROR;
  memcpy (fn_mutable, file_name, PGSIZE);
  /* Set process 'magic header', so that 
     thread_create can determine it is a process or kernel thread. */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  memcpy (fn_copy, &PROCESS_MAGIC, sizeof(int)); 
  strlcpy (fn_copy+sizeof(int), file_name, PGSIZE-sizeof(int));

  /* Get the executable name */
  exec_name = strtok_r (fn_mutable, " ",  &save_ptr);  

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (exec_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  palloc_free_page (fn_mutable);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  struct thread *t = thread_current();
#ifdef VM
  page_table_init (&t->page_table);
#endif
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  lock_acquire (&fs_lock);
  success = load (file_name, &if_.eip, &if_.esp);
  lock_release (&fs_lock);
  /* If load failed, quit. */
  palloc_free_page (file_name-sizeof(int));
  if (!success) { 
    thread_current ()->process_exit_status = -1;
    thread_exit ();//the thread_exit will notifiy the waiting processes
  } else {
    /* Notifiy the parent */
    t->parent->child_load_success = success;
    sema_up (&t->parent->wait_child_load);
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  //hex_dump (if_.esp, if_.esp, 0xc0000000-(uint32_t)if_.esp, true);
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current ();
  struct thread *child = thread_get_by_id (child_tid);
  if (!child && child_tid < 64 && child_tid >= 0)  {
    int retval = cur->child_exit_status[child_tid];
    cur->child_exit_status[child_tid] = -1;
    return retval;
  }
    
  if (!child 
      || !child->is_process 
      || child->parent != thread_current ()
      || child->is_already_call_wait)
    return -1;
  if (child_tid < 64 && child_tid >= 0)
    cur->child_exit_status[child_tid] = -1;
  child->is_already_call_wait = true;
  if (child && child->process_exit_status == EXIT_NOT_EXIT
      || cur->wait_exit_status == EXIT_NOT_EXIT) //still running
    sema_down (&child->being_waited);//wait for it
  int retval = cur->wait_exit_status;
  cur->wait_exit_status = EXIT_NOT_EXIT;
  return retval;
}

static int
begin_debug (void) {
  printf ("Begin Debug\n");
}

/* Free the current process's resources. It may be a bad design to let kernel to do so. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();

  /* close all files opened by process */
  while (!list_empty (&cur->opened_files)) {
    struct list_elem *e = list_pop_front (&cur->opened_files);
    struct file_info *fi = list_entry (e, struct file_info, elem);
    //lock_acquire (&fs_lock);
    file_close (fi->fptr);
    //lock_release (&fs_lock);
    free (fi);
  }

#ifdef VM
  if (cur->is_process) {
      process_munmap_file (-1);
      page_table_destory (&cur->page_table);
  } 
#endif


  /* Close the executable */
  if (cur->executable) {
    //lock_acquire (&fs_lock);
    file_allow_write (cur->executable);
    file_close (cur->executable);
    //lock_release (&fs_lock);
  }
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
  if (cur->is_process) {
    if (!cur->is_already_call_wait && cur->tid < 64)
      cur->parent->child_exit_status[cur->tid] = cur->process_exit_status;
    else if (cur->tid < 64)
      cur->parent->child_exit_status[cur->tid] = -1;
    printf ("%s: exit(%d)\n",thread_name(), cur->process_exit_status);

    /* If current process owns lock, release it */
    if (fs_lock.holder == cur)
      lock_release (&fs_lock);

    /* Notify processes waiting for it */
    struct list_elem *e;
    for (e = list_begin (&cur->being_waited.waiters); e != list_end (&cur->being_waited.waiters); e = list_next (e)) {
      struct thread *t = list_entry (e, struct thread, elem);
      t->wait_exit_status = cur->process_exit_status;
    }
    if (list_empty (&cur->being_waited.waiters))
      cur->parent->wait_exit_status = cur->process_exit_status+1;
    sema_up (&cur->being_waited);
    sema_up (&cur->parent->wait_child_load);
    
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char *save_ptr;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */  
  char *exec_name = strtok_r ((char*)file_name, " ",  &save_ptr);
  
  file = filesys_open (exec_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
  thread_current() ->executable = file;
  file_deny_write (file);
  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Arguments parsing onto the stack */
  char *argv = NULL;
  int argc = 0, argvs_size = 2;
  char **argvs = palloc_get_page (0);
  char **argvs_str = palloc_get_page (0);
  /*
    Splict the argument strings into arguments,
    and push them on to the stack(with random sequences), 
    save the stack pointers in the container ARGVS
  */
  for (argv = exec_name;argv != NULL; argv = strtok_r (NULL, " ", &save_ptr)) {
    //push (esp, argv, strlen(argv)+1);
    //argvs[argc++] = *esp;
    argvs_str[argc++] = argv;
  }
  for (i = argc-1; i >= 0; i--) {
    push (esp, argvs_str[i], strlen (argvs_str[i])+1);
    argvs[i] = *esp;
  }
  argvs[argc] = 0; //the last one is 0 as the standard C requires.

  /* Then try word-align */
  i = (size_t)*esp % 4;
  if (i)
    push (esp, &argvs[argc], i);

  /* Push the ARGVS onto the stack from right to left */
  for (i = argc; i >= 0; i--)
    push (esp, &argvs[i], sizeof(char *));

  argv = *esp;
  push (esp, &argv, sizeof (char **));
  
  /* push ARGC */
  push (esp, &argc, sizeof (int));
  
  /* push return address, 0 at this case */
  push (esp, &argvs[argc], sizeof (void (*) (void)));
  /* free all memory */
  palloc_free_page (argvs_str);
  palloc_free_page (argvs);
  
  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

static void 
push (void **esp, void *data, size_t length) {
  *esp -= length;
  memcpy (*esp, data, length);
}


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  size_t file_start = ofs;

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
#ifndef VM
      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }
#else       /* Lazy load */

      /* Setup page */
      struct page uvpage;
      uvpage.uvaddr = upage;
      uvpage.fptr = file;
      uvpage.frs = page_read_bytes;
      uvpage.fs = file_start;
      uvpage.fzs = page_zero_bytes;
      uvpage.flags = writable ? UPG_WRITABLE : 0;
      /* Insert into page table */
      page_table_insert (&thread_current ()->page_table, &uvpage);

      /* Adcance */
      file_start += page_read_bytes;
#endif
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;
#ifndef VM
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
#else
  success = page_user_stack (((uint8_t *) PHYS_BASE) - PGSIZE);
 if (success)
   *esp = PHYS_BASE;
#endif
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

int 
process_add_openfile (struct file *fptr) {
  if (fptr == NULL)
    return -1;
  struct file_info *fi = malloc (sizeof (struct file_info));
  fi->fptr = fptr;
  struct thread *cur = thread_current ();
  //fi->fd = 2 + list_size (&cur->opened_files);
  if (list_empty (&cur->opened_files))
    fi->fd = 2;
  else {
    struct list_elem *e = list_back (&cur->opened_files);
    fi->fd = list_entry (e, struct file_info, elem)->fd+1;
  }
  fi->offset = 0;
  list_push_back (&(cur->opened_files), &fi->elem);
  return fi->fd;
}

#ifdef VM
bool
process_mmap_file (struct page *uvpage) {
  struct mmap_file *mfptr = (struct mmap_file*)malloc (sizeof (struct mmap_file));
  struct thread *cur = thread_current ();
  if (mfptr) {
    mfptr->map_page = uvpage;
    mfptr->id = cur->mapid;
    list_push_back (&cur->mmap_file, &mfptr->elem);
    return true;
  }
  return false;
}

void
process_munmap_file (mmapid_t mapid) {
  struct thread *cur = thread_current ();  
  size_t map_size = list_size (&cur->mmap_file);
  int close = 0, i;
  struct file *f = NULL;
  
  for (i = 0; i < map_size; i++) {
    struct list_elem *e = list_pop_front (&cur->mmap_file);
    struct mmap_file *mfptr = list_entry (e, struct mmap_file, elem);
    if (mfptr->id == mapid || mapid == -1) { //if current map page should be delete
      mfptr->map_page->flags &= ~UPG_EVICTABLE; //mark it as Unevictable
      if (!(mfptr->map_page->flags & UPG_INVALID)) { //if this page has data
        if (pagedir_is_dirty (cur->pagedir, mfptr->map_page->uvaddr)) { //and it is dirty
          /* Write back to file */
          //lock_acquire (&fs_lock);
          file_write_at (mfptr->map_page->fptr, mfptr->map_page->uvaddr,
                         mfptr->map_page->frs, mfptr->map_page->fs);
          //lock_release (&fs_lock);
        }
        /* Clear the uvpage and free the kpage */
        frame_free_page (pagedir_get_page (cur->pagedir, mfptr->map_page->uvaddr));
        pagedir_clear_page (cur->pagedir, mfptr->map_page->uvaddr);
      }
      //if this page has data end
      /* Remove from page table */
      hash_delete (&cur->page_table, &mfptr->map_page->elem);
      /* Awesome way */
      if (close != mfptr->id) {
        if (f) {
          lock_acquire (&fs_lock);
          file_close (f);
          lock_release (&fs_lock);
        }
        close = mfptr->id;
        f = mfptr->map_page->fptr;
      }
      free (mfptr->map_page);
      free (mfptr);

    } else /* If this page should not be deleted, push back to map file list */
      list_push_back (&cur->mmap_file, e);
  }
  /* Awesome way */
  if (f) {
    //lock_acquire (&fs_lock);
    file_close (f);
    //lock_release (&fs_lock);
  }
}

#endif
