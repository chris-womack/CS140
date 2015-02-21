#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/synch.h"

#ifndef EXIT_NOT_EXIT
#define EXIT_NOT_EXIT 128
#endif

#define MAX_CHILD_SIZE 64

/* States in a thread's life cycle. */

enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING      /* About to be destroyed. */
  };

enum compare_order {
  THREAD_COMPARE_ASC,
  THREAD_COMPARE_DEC,
  THREAD_COMPARE_SLEEP_END
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* Data Structure for representing a file owns by a process */
struct file_info {
  struct file* fptr; /* Pointer for file struct created by filesys */
  int fd;            /* File Desription */
  uint32_t offset;  /* Current process's offset on this file */
  struct list_elem elem; /* List elem */
};

/* Implement function to use list_insert_order. */
bool file_info_compare( const struct list_elem *a, const struct list_elem *b, void *aux );

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

   4 kB +---------------------------------+
   |          kernel stack           |
   |                |                |
   |                |                |
   |                V                |
   |         grows downward          |
   |                                 |
   |                                 |
   |                                 |
   |                                 |
   |                                 |
   |                                 |
   |                                 |
   |                                 |
   +---------------------------------+
   |              magic              |
   |                :                |
   |                :                |
   |               name              |
   |              status             |
   0 kB +---------------------------------+

   The upshot of this is twofold:

   1. First, `struct thread' must not be allowed to grow too
   big.  If it does, then there will not be enough room for
   the kernel stack.  Our base `struct thread' is only a
   few bytes in size.  It probably should stay well under 1
   kB.

   2. Second, kernel stacks must not be allowed to grow too
   large.  If a stack overflows, it will corrupt the thread
   state.  Thus, kernel functions should not allocate large
   structures or arrays as non-static local variables.  Use
   dynamic allocation with malloc() or palloc_get_page()
   instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                          /* Thread identifier. */
  enum thread_status status;          /* Thread state. */
  char name[16];                      /* Name (for debugging purposes). */
  uint8_t *stack;                     /* Saved stack pointer. */
  int priority;                       /* Priority. */
  struct list_elem allelem;           /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem;              /* List element. */

  int64_t sleep_end;                 /* Sleep End Ticks */
  struct list waiting_locks;      /* Use by priority-donate-multiple, list of lock */
  struct list owning_locks;       
#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir;                  /* Page directory. */
  int process_exit_status;      /* Interrupt return status */
  struct semaphore wait_child_load;  /* Semaphore for wating child to load executable */
  struct thread *parent;            /* Parent process */
  bool child_load_success;         /* Flag for child process loading */
  bool is_process;            /* Flag to detect whether it is process or kernel thread */
  bool is_already_call_wait; /* Flag to detect whether it has been called process_wait */
  struct list opened_files;       /* List for files opened by process */  
  struct semaphore being_waited; /* Semaphore for being waited by parent */
  struct file *executable;       /* Pointer to current process's executable */
  int wait_exit_status;      /* Status return from exited waiting process. */
  //int *child_exit_status;   
  int child_exit_status[MAX_CHILD_SIZE];
#endif

#ifdef VM /* Owened by process.c and vm */
  struct list mmap_file;
  struct hash page_table;
  int mapid;
#endif

  /* Owned by thread.c. */
  unsigned magic;                     /* Detects stack overflow. */

  /* Use for 4.4BSD Scheduler */
  int nice;
  int recent_cpu;  
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/// @describe thread's list_less_func implementation
bool thread_compare( const struct list_elem *a, const struct list_elem *b, void *aux );

void print_thread_list( const char *label, const struct list *toPrint );

void thread_preemption (void);

void thread_recalc_priority (struct thread *t, void *aux);

void thread_inc_recent_cpu(void);

void thread_recalc_recent_cpu(struct thread *t, void *aux);

void thread_recalc_load_avg(void);

/* Return NULL if TID not found in all_list */
struct thread *thread_get_by_id (tid_t tid);

#endif /* threads/thread.h */
