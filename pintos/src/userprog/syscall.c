#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
/* Functions prototype for system calls framework */
static void _halt (void);
static void _exit (int status);
static tid_t _exec (const char *cmd_lin);
static int _wait (tid_t p);
static bool _create (const char *file, unsigned initial_size);
static bool _remove (const char *file);
static int _open (const char *file);
static int _filesize (int fd);
static int _read (int fd, void *buffer, unsigned length);
static int _write (int fd, const void *buffer, unsigned length);
static void _seek (int fd, unsigned position);
static unsigned _tell (int fd);
static void _close (int fd);
#ifdef VM
static mmapid_t _mmap (int fd, void *addr);
static void _munmap (mmapid_t mapid);
#endif

syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&fs_lock);
}

/*
   Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred.
*/
static int
get_user (const uint8_t *uaddr) {
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a"  (result)
       : "m" (*uaddr));
  return result;
}

static uint32_t
syscall_get_argument (uint32_t *esp, int number) {
  check_valid_ptr (esp+number, sizeof(uint32_t));
  return *(esp+number);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *sp = f->esp; //suppose all arguments are 4 bytes
  switch (syscall_get_argument (sp, 0)) {
  case SYS_HALT:
    _halt ();
    break;
    
  case SYS_EXIT:
    _exit (syscall_get_argument (sp, 1));
    break;
    
  case SYS_EXEC:
    f->eax = _exec (syscall_get_argument (sp, 1));
    break;

  case SYS_WAIT:
    f->eax = _wait (syscall_get_argument (sp, 1));
    break;
    
  case SYS_CREATE:
    f->eax = _create (syscall_get_argument (sp, 1), 
                      syscall_get_argument (sp, 2));
    break;
    
  case SYS_REMOVE:
    f->eax = _remove (syscall_get_argument (sp, 1));
    break;

  case SYS_OPEN:
    f->eax = _open (syscall_get_argument (sp, 1));
    break;

  case SYS_FILESIZE:
    f->eax = _filesize (syscall_get_argument (sp, 1));
    break;

  case SYS_READ:
    f->eax = _read (syscall_get_argument (sp, 1),
                    syscall_get_argument (sp, 2),
                    syscall_get_argument (sp, 3));
    break;

  case SYS_WRITE:
    f->eax = _write (syscall_get_argument (sp, 1),
                     syscall_get_argument (sp, 2),
                     syscall_get_argument (sp, 3));
    break;

  case SYS_SEEK:
    _seek (syscall_get_argument (sp, 1),
           syscall_get_argument (sp, 2));
    break;

  case SYS_TELL:
    f->eax = _tell (syscall_get_argument (sp, 1));
    break;

  case SYS_CLOSE:
    _close (syscall_get_argument (sp, 1));
    break;
#ifdef VM
  case SYS_MMAP:
    _mmap (syscall_get_argument (sp, 1), 
           syscall_get_argument (sp, 2));
    break;

  case SYS_MUNMAP:
    _munmap (syscall_get_argument (sp, 1));
    break;
#endif
  case SYS_CHDIR:
    //Others for project4
    break;
    
  case SYS_MKDIR:
    break;

  case SYS_READDIR:
    break;

  case SYS_ISDIR:
    break;

  case SYS_INUMBER:
    break;
  }
}

/* 
   Terminate Pintos by calling SHUTDOWN_POWER_OFF(declared in device/shutdown.h).
   This should be seldom used, because you lose some information about possible deadlock 
   situations, etc.
*/
static void
_halt (void){
  shutdown_power_off();
}

/* Terminates the current user program, returning STATUS to the kernel. */
static void 
_exit (int status) {
  thread_current()->process_exit_status = status;
  thread_exit ();
}

/* 
   Run the executable whose name is givin in CMD_LINE, 
   and return the new process's program id.
   Must return -1 if the program can not load or run. 
   Thus, parent process cannot return from exec 
   until it knows whether the child process successfully load its executable. 
*/
static tid_t
_exec (const char *cmd_line) {
  check_valid_ptr (cmd_line, 1);
  check_valid_ptr (cmd_line+1, strlen(cmd_line));

  tid_t new_process = process_execute (cmd_line);
  if (new_process == TID_ERROR)
    return -1;

  /* Wait child to finish load(). */
  sema_down (&thread_current()->wait_child_load);
  if (thread_current()->child_load_success)
    return new_process;
  else
    return -1;
}

/*
  Waits for a child process pid and retrieves the child’s exit status.
  If pid is still alive, waits until it terminates. 
  Then, returns the status that pid passed to exit.If pid did not call exit(), 
  but was terminated by the kernel (e.g. killed due to an exception), 
  wait(pid) must return -1.
  It is perfectly legal for a parent process to wait for child processes 
  that have already terminated by the time the parent calls wait, 
  but the kernel must still allow the parent to retrieve its child’s exit status, 
  or learn that the child was terminated by the kernel.
*/
static int
_wait (tid_t p) {
  return process_wait (p);
}

static bool 
_create (const char *file, unsigned initial_size) {
  check_valid_ptr (file, 1); //ensure argument passing to strlen is valid
  check_valid_ptr (file+1, strlen (file));

  lock_acquire (&fs_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&fs_lock);
  return success;
}

static bool
_remove (const char *file) {
  check_valid_ptr (file, 1); //ensure argument passing to strlen is valid
  check_valid_ptr (file+1, strlen (file));

  lock_acquire (&fs_lock);
  bool success = filesys_remove (file);
  lock_release (&fs_lock);
  return success;
}

static int 
_open (const char *file) {
  check_valid_ptr (file, 1); //ensure argument passing to strlen is valid
  check_valid_ptr (file+1, strlen (file));

  lock_acquire (&fs_lock);
  struct file *op_fptr = filesys_open (file);
  lock_release (&fs_lock);
  int fd = process_add_openfile (op_fptr);

  return fd;
}

/* return -1 if fd not exit in current process's open fles */
static int 
_filesize (int fd) {
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
       e = list_next (e)) {
    struct file_info *fi = list_entry (e, struct file_info, elem);
    if ( fi->fd == fd ) {
      lock_acquire (&fs_lock);
      int fs = file_length (fi->fptr);
      lock_release (&fs_lock);
      return fs;
    }
  }
  return -1;
}

static int 
_read (int fd, void *buffer, unsigned length) {
  check_valid_ptr (buffer, length);
  
  if (fd == STDIN_FILENO) {
    //stdin
    int i;
    for (i = 0; i < length; i++)
      *(uint8_t *)buffer++ = input_getc();
    return i;
  } else {
    //opened file
    struct list_elem *e;
    struct thread *cur = thread_current ();
    for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
         e = list_next (e)) {
      struct file_info *fi = list_entry (e, struct file_info, elem);
      if (fi->fd == fd) {
        lock_acquire (&fs_lock);
        int read_size = file_read (fi->fptr, buffer, length);
        lock_release (&fs_lock);
        return read_size;
      }
    }
  }
  return -1;
}

static int 
_write (int fd, const void *buffer, unsigned length) {
  check_valid_ptr (buffer, length);
  if (fd == STDOUT_FILENO) { 
    //write to console
    putbuf (buffer, length);
    return length;
  } else {
    //opened file
    struct list_elem *e;
    struct thread *cur = thread_current ();
    for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
         e = list_next (e)) {
      struct file_info *fi = list_entry (e, struct file_info, elem);
      if (fi->fd == fd) {
        lock_acquire (&fs_lock);
        int write_size = file_write (fi->fptr, buffer, length);
        lock_release (&fs_lock);
        return write_size;
      }
    }
  }
  return -1;
}

static void 
_seek (int fd, unsigned position) {
  if (position < 0)
    _exit (-1);

  struct list_elem *e;
  struct thread *cur = thread_current ();
  for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
       e = list_next (e)) {
    struct file_info *fi = list_entry (e, struct file_info, elem);
    if (fi->fd == fd) {
      lock_acquire (&fs_lock);
      int32_t length = file_length (fi->fptr);
      if (position > length)
        position = (unsigned)length;

      file_seek (fi->fptr, position);
      lock_release (&fs_lock);
      return;
    }
  }
  //it does nothing when fd provided doesn't exist in current process's openfiles list
}

static unsigned
_tell (int fd) {
  struct list_elem *e;
  struct thread *cur = thread_current ();
  for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
       e = list_next (e)) {
    struct file_info *fi = list_entry (e, struct file_info, elem);
    if (fi->fd == fd) {
      lock_acquire (&fs_lock);
      int32_t position = file_tell (fi->fptr);
      lock_release (&fs_lock);
      return position;
    }
  }
  return -1;
}

static void 
_close (int fd) {
  struct list_elem *e;
  struct thread *cur = thread_current ();
  for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
       e = list_next (e)) {
    struct file_info *fi = list_entry (e, struct file_info, elem);
    if (fi->fd == fd) {
      e = list_remove (&fi->elem);
      lock_acquire (&fs_lock);
      file_close (fi->fptr);
      lock_release (&fs_lock);
      free (fi);
      break;
    }
  }
}

#ifdef VM
static mmapid_t 
_mmap (int fd, void *addr) {
  check_valid_ptr (addr, PGSIZE);
  struct list_elem *e;
  struct thread *cur = thread_current ();
  struct page p;
  size_t file_start = 0, read_bytes = 0, page_read_bytes = 0, page_zero_bytes = 0;
  for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
       e = list_next (e)) {
    struct file_info *fi = list_entry (e, struct file_info, elem);
    if (fi->fd == fd) {
      lock_acquire (&fs_lock);
      struct file *f = file_reopen (fi->fptr);
      read_bytes = file_length (fi->fptr);
      lock_release (&fs_lock);
      cur->mapid++;
      while (read_bytes > 0) {
        page_read_bytes = read_bytes > PGSIZE ? PGSIZE : read_bytes;
        page_zero_bytes = PGSIZE - page_read_bytes;
        p.uvaddr = addr, p.frs = page_read_bytes;
        p.fs = file_start, p.fzs = page_zero_bytes;
        p.flags = UPG_ON_MMAP | UPG_WRITABLE, p.fptr = f;
        if (page_table_insert (&cur->page_table, &p)) {
          /* Advance */
          read_bytes -= page_read_bytes;
          file_start += page_read_bytes;
          addr += page_read_bytes;
          return cur->mapid;
        } else
          _munmap (cur->mapid);
      }
      break;
    }
  }
  return -1;
}

static void 
_munmap (mmapid_t mapid) {
  process_munmap_file (mapid);
}

#endif

void 
check_valid_ptr (char *ptr, size_t size) {
  size_t i;
  for (i = 0; i < size; i++, ptr++)
    if (!is_user_vaddr (ptr) 
        || ptr == NULL 
        || get_user (ptr) == -1)
      _exit (-1);

}
