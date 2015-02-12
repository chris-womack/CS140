#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"

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

void
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

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. 
*/
static bool
put_user (uint8_t *udst, uint8_t byte) {
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       :"=&a" (error_code), "=m" (*udst)
       : "q" (byte));
  return error_code != -1;
}

static uint32_t
syscall_get_argument (uint32_t *esp, int number) {
  /* esp += number; */
  /* uint8_t *byte_esp = (uint8_t *)esp; //we need to move pointer one byte at each step */
  /* uint32_t i, result = 0; */
  /* for (i = 0; i < 4; i++) { */
  /*   int temp = get_user (byte_esp+i); */
  /*   if (temp == -1) */
  /*     _exit (-1); */
  /*   else */
  /*     result += temp << (8*i); */
  /* } */
  /* return result; */
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

void 
check_valid_ptr (char *ptr, size_t size) {
  size_t i;
  for (i = 0; i < size; i++, ptr++) {
    if (!is_user_vaddr (ptr))
      _exit (-1);
    else if (ptr == NULL)
      _exit (-1);
    else if (get_user (ptr) == -1)
      _exit (-1);
    }
}
