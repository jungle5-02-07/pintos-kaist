#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
// fork를 위한 semaphore
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* Define Macro for floating point calculate */
#define F (1 << 14) // define fixed-point scale factor 2^14
#define CONV_TO_FIXED(n) ((n) * F)
#define CONV_TO_INT_ZERO(x) ((x) / F) // x를 정수로 변환 (0 방향으로 반올림)
#define CONV_TO_INT_NEAREST(x) ((x) >= 0 ? ((x) + F / 2) / F : ((x) - F / 2) / F) // x를 정수로 변환 (가장 가까운 정수로 반올림)
#define ADD(x, y) ((x) + (y)) // x와 y를 더하기
#define SUBTRACT(x, y) ((x) - (y)) // y를 x에서 빼기
#define ADD_X_N(x, n) ((x) + (n) * F) // x에 n을 더하기 (n은 정수)
#define SUBTRACT_X_N(x, n) ((x) - (n) * F) // x에서 n을 빼기 (n은 정수)
#define MULTIPLY(x, y) ((int64_t)(x) * (y) / F) // x와 y를 곱하기
#define MULTIPLY_X_N(x, n) ((x) * (n)) // x에 n을 곱하기 (n은 정수)
#define DIVIDE(x, y) (((int64_t)(x) * F) / (y)) // x를 y로 나누기
#define DIVIDE_X_N(x, n) ((x) / (n)) // x를 n으로 나누기 (n은 정수)

/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

// syscall 관련 매크로
#define FDT_PAGES 3 // 파일디스크립터 테이블에 할당할 페이지 수 (thread_create, process_exit)
#define FDCOUNT_LIMIT FDT_PAGES*(1<<9) // 파일 디스크립터 테이블의 최대 크기

// 파일디스크립터 구조체
struct file_fd
{
	int fd;					  /* fd: 파일 식별자 */
	struct file *file;		  /* file */
	struct list_elem fd_elem; /* list 구조체의 구성원 */
};

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	int original_priority;              /* Original Priority */

  struct lock *wait_on_lock;		
	struct list donations;							/* Donation data struct */
	int nice;
	int recent_cpu;

	int64_t sleep_ticks;                 /* check threads wake up time */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	struct list_elem d_elem;				/* Donation element */
	struct list_elem all_elem;

	// system call 관련 멤버 추가
	int exit_status;

	struct list *fd_table[FDCOUNT_LIMIT]; // thread_create에서 할당
	int fd_idx; // fd테이블 인덱스, file descriptor
	struct file *now_file;

	struct semaphore fork_sema;
	struct semaphore wait_sema;
	struct semaphore exit_sema;

	struct list_elem child_elem;

	struct intr_frame parent_if;
	
	// get_child_with_pid 를 위해 추가
	struct list child_list;

	bool check_child;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);

int64_t get_global_ticks(void);
void set_global_ticks(int64_t new_ticks);

void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

bool compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

void thread_sleep(int64_t ticks);
void thread_awake (int64_t ticks);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

void check_preemption(void);

void increase_recent_cpu (void);
void calc_priority (struct thread* t);
void calc_recent_cpu (struct thread* t);
void calc_load_average (void);

void recalc_priority (void);
void recalc_all (void);

#endif /* threads/thread.h */
