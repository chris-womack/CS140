#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* Functions prototype for system calls framework */
void _halt (void);
void _exit (int status);
tid_t _exec (const char *cmd_lin);
int _wait (tid_t p);
bool _create (const char *file, unsigned initial_size);
bool _remove (const char *file);
int _open (const char *file);
int _filesize (int fd);
int _read (int fd, void *buffer, unsigned length);
void _seek (int fd, unsigned position);
unsigned _tell (int fd);
void _close (int fd);

/* Function to check pointer provided by user.
   If PTR is invalid, (maybe tell the user and)exit the process.
 */
static void check_valid_ptr (void *ptr);

/* Function to check a valid user file desriptor */
static bool is_valid_fd (int fd);

/* Function to read arguments for system call(works for intr handle too) */
uint32_t syscall_get_argument (uint32_t *esp, int number);




#endif /* userprog/syscall.h */
