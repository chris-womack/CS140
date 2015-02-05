#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

uint32_t
syscall_get_argument (uint32_t *esp, int number) {
  check_valid_ptr (esp+offset);
  return *(esp+offset);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *sp = f->esp; //suppose all arguments are 4 bytes
  
  switch (*sp) {
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
    f->eax = _read (syscall_get_argument (sp, 1));
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

  case SYS_MMAP:
    //TODO rest in after project 2
    break;

  case SYS_MUNMAP:
    break;

  case SYS_CHDIR:
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
void
_halt (void){
  shutdown_power_off();
}

/* Terminates the current user program, returning STATUS to the kernel. */
void 
_exit (int status) {
  thread_current()->intr_ret_status = status;
  thread_exit ();
}

/* 
   Run the executable whose name is givin in CMD_LINE, 
   and return the new process's program id.
   Must return -1 if the program can not load or run. 
   Thus, parent process cannot return from exec 
   until it knows whether the child process successfully load its executable. 
*/
tid_t
_exec (const char *cmd_line) {
  check_valid_ptr (cmd_line);
  check_valid_ptr (cmd_line+strlen (cmd_line));

  tid_t new_process = process_execute (cmd_line);
  if (new_process == TID_ERROR)
    return -1;

  /* waiting for child to load */  
  sema_down (&thread_current()->wait_child_load);
  if (thread_current()->child_load_success)
    return new_process;
  else
    return -1;
}

int
_wait (tid_t p) {
  return process_wait (p);
}

bool 
_create (const char *file, unsigned initial_size) {
  check_valid_ptr (file);
  check_valid_ptr (file + strlen (file));
  lock_acquire (&fs_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&fs_lock);
  return success;
}

bool _remove (const char *file) {
  check_valid_ptr (file);
  check_valid_ptr (file + strlen (file));
  lock_acquire (&fs_lock);
  bool success = filesys_remove (file);
  lock_release (&fs_lock);
  return success;
}

int _open (const char *file) {
  check_valid_ptr (file);
  check_valid_ptr (file + strlen (file));
  lock_acquire (&fs_lock);
  struct file *op_fptr = filesys_open (file);
  int fd = process_add_openfile (op_fptr);
  lock_release (&fs_lock);
  return fd;
}

/* return -1 if fd not exit in current process's open fles */
int 
_filesize (int fd) {
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (cur->opened_files); e != list_end (cur->opened_files);
       e = list_next (e)) {
    struct file_info *fi = list_entry (e, struct file_info, elem);
    if ( fi->fd == fd ) {
      lock_acquire (&fs_lock);
      int fs = file_length (fi->fptr);
      lock_release (&fs_lock);
      return fs;
    }
  }
  return 0;
}

int 
_read (int fd, void *buffer, unsigned length) {
  check_valid_ptr (buffer);
  check_valid_ptr (buffer+length);
  
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

int 
_write (int fd, const void *buffer, unsigned length) {
  check_valid_ptr (buffer);
  check_valid_ptr (buffer + length);
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
        int write_size = file_write (fi->fptr, buffer, size);
        lock_release (&fs_lock);
        return write_size;
      }
    }
  }
  return -1;
}

void 
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

unsigned
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

void 
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

static void 
check_valid_ptr (void *ptr) {
  if (!is_user_vaddr (ptr)
      || ptr == NULL) {
    printf ("Bad Access Memory %p\n",ptr);
    _exit (-1);
  }
}
