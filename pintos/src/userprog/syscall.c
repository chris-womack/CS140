#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

static void check_valid_user_ptr (void *ptr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

uint32_t syscall_get_argument (uint32_t *esp, int offset) {
  check_valid_user_ptr (esp+offset);
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
    //f->eax = _file_size (syscall_get_argument (sp, 1));
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
    //f->eax = _tell (syscall_get_argument (sp, 1));
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
void _halt (void){
  shutdown_power_off();
}

/* Terminates the current user program, returning STATUS to the kernel. */
void _exit (int status) {
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
tid_t _exec (const char *cmd_line) {
  check_valid_user_ptr (cmd_line);

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

int _wait (pid_t) {
  return -1;
}

int _create (const char *file, unsigned initial_size) {
  return -1;
}

int _remove (const char *file) {
  return -1;
}

int _open (const char *file) {
  return -1;
}
int _filesize (int fd) {
  return -1;
}

int _read (int fd, void *buffer, unsigned length) {
  return -1;
}
int _write (int fd, const void *buffer, unsigned length) {
  int result = 0;
  check_valid_user_ptr (buffer);
  check_valid_user_ptr (buffer + length);
  if (fd == STDOUT_FILENO) { //write to console
    putbuf (buffer, length);
    result = length;
  }
  return length;
}

void _seek (int fd, unsigned position) {

}
unsigned _tell (int fd) {
  return 0;
}

void _close (int fd) {

}

static void check_valid_user_ptr (void *ptr) {
  if (!is_user_vaddr (ptr)
      || ptr == NULL)
    _exit (-1);
}
