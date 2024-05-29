#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* Set list about all thread regardless thread status */
static struct list all_list;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

static struct list sleep_list;

/* Set Global Ticks for sleep and awake */
static int64_t global_ticks;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

static int load_avg;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */

/* 실행 중인 스레드를 반환합니다. 
 * CPU의 스택 포인터 'rsp'를 읽은 다음 페이지 시작 부분까지 반올림합니다.
 * 'struct thread'는 항상 페이지 시작 부분에 있고
 * 스택 포인터는 중간 어딘가에 있기 때문에 current 스레드를 찾습니다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&all_list); // 모든 스레드에 대한 리스트 초기화
	list_init (&ready_list); // 준비 큐 초기화
	list_init (&sleep_list); // 대기 큐 초기화
	list_init (&destruction_req);

	/* Init the global variable */
	load_avg = 0;

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

int64_t get_global_ticks (void) {
	return global_ticks;
}

/* ticks setter */
void set_global_ticks ( int64_t new_ticks ) {
	global_ticks = new_ticks;
	return;
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	struct thread *cur = thread_current();

	t -> parent_id = cur -> tid; // 부모 프로세스 정보 추가
	t -> parent_process = cur; // 부모 프로세스 추가

	t -> is_load = false;
	t -> is_exit = false;

	sema_init(&t -> exit_sema, 0);
	sema_init(&t -> load_sema, 0);

	list_push_back(&cur -> child_list, &t -> child_elem); // 부모 프로세스의 자식 프로세스 리스트에 추가


	/* 파일 디스크립터 관련 자료 초기화 */
	t -> fd_table = (struct file**)malloc(sizeof(struct file*) * MAX_TABLE_SIZE); // Fd Table 메모리 할당

	for (int i = 3; i < MAX_TABLE_SIZE; i++) {
			t -> fd_table[i] = NULL;
	}

	t -> fd = 3; // Fd 초기값 설정


	/* Add to run queue. */
	thread_unblock (t);

	/* Compare the priorites of the currently running thread and the newly instered one.
	*  Yield the CPU if the newly arriving thtead has higher priority */
	check_preemption();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);

	list_insert_ordered(&ready_list, & t -> elem, compare_priority, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

bool
compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux) {

	struct thread *a_thread = list_entry(a, struct thread, elem);
	struct thread *b_thread = list_entry(b, struct thread, elem);

	return a_thread -> priority > b_thread -> priority;
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();

	sema_up(&thread_current() -> exit_sema);

	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current (); // 현재 스레드를 Return
	enum intr_level old_level;

	ASSERT (!intr_context ()); //

	old_level = intr_disable (); // 인터럽트를 비활성화 하고 이전 인터럽트 상태를 전달
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, & curr -> elem, compare_priority, NULL); // priority-preempt
		// list_push_back (&ready_list, &curr->elem); // 대기 큐로 삽입
	do_schedule (THREAD_READY); // 대기 상태로 전환
	intr_set_level (old_level); // 인터럽트 상태에 따라 인터럽트를 활성/비활성화 하고 이전 인터럽트 상태로 설정
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	if ( thread_mlfqs ) return;
	
	struct thread *t = thread_current();

	t -> original_priority = new_priority; // 스레드의 원래 우선순위를 갱신
	t -> priority = get_donation_priority(t); // 우선순위 양도를 고려한 우선 순위 갱신
	donate_nested_priority (t);

	check_preemption();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Control Blocked Thread with sleep queue*/
void
thread_sleep(int64_t ticks) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;
	ASSERT(!intr_context());

	old_level = intr_disable (); // 인터럽트 OFF

	if (curr != idle_thread) { // 현재 스레드가 idle thread인지 확인
		curr -> sleep_ticks = ticks; //  tick 추가 for wake up
		list_push_back(&sleep_list, &curr -> elem);  // 대기 큐로 변수 할당
		if ( get_global_ticks() > curr -> sleep_ticks ) 
				set_global_ticks( curr -> sleep_ticks );
		thread_block(); // block + 새로운 스케줄 생성 ( 내부에서 현재 스레드를 Running으로 바꿔줌 )
	}
	
	intr_set_level (old_level); // 인터럽트 ON

	return;
}

void thread_awake (int64_t ticks) { // 인자로 global tick 받을 에정
	struct list_elem *e = list_begin(&sleep_list);

	set_global_ticks(INT64_MAX);
	while ( e != list_end(&sleep_list) ) // 해당 리스트의 마지막 요소 ( 가드 노드 )까지 순회하며 탐색
	{
		// 현재 list_elem에서 원본 스레드 구조체로의 포인터를 얻음
		struct thread *t = list_entry(e, struct thread, elem);
		
		if ( t -> sleep_ticks <= ticks ) { // global ticks 보다 작으면
			e = list_remove(e); // pop을 못해서 임의로 리스트에서 삭제 ( 삭제후 e를 next로 이동 )
			thread_unblock(t); // 해당 스레드의 상태 변경

		} else {
			if ( t -> sleep_ticks < get_global_ticks() ) {
				set_global_ticks(t -> sleep_ticks);  // 전역 틱 갱신
			}
			e = list_next(e);
		}
	}
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	enum intr_level old_level;
	old_level = intr_disable (); // 인터럽트 OFF

	struct thread* cur = thread_current();
	cur -> nice = nice;

	calc_priority(cur);
	check_preemption();
	
	intr_set_level (old_level); // 인터럽트 ON

}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	enum intr_level old_level;
	old_level = intr_disable (); // 인터럽트 OFF

	int cur_nice = thread_current() -> nice;
	
	intr_set_level (old_level); // 인터럽트 ON

	return cur_nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	enum intr_level old_level;
	int cur_load_avg;

	old_level = intr_disable (); // 인터럽트 OFF
	
	cur_load_avg = CONV_TO_INT_NEAREST(load_avg * 100);

	intr_set_level (old_level); // 인터럽트 ON

	return cur_load_avg;
}

int
thread_get_recent_cpu (void) {
	enum intr_level old_level;
	int cur_recent_cpu;

	old_level = intr_disable (); // 인터럽트 OFF
	
	cur_recent_cpu = CONV_TO_INT_NEAREST(thread_current() -> recent_cpu * 100);

	intr_set_level (old_level); // 인터럽트 ON

	return cur_recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->original_priority = priority; // 원래 우선순위를 할당
	t->magic = THREAD_MAGIC;
	t->wait_on_lock = NULL; // LOCK 관련 데이터 초기화
	list_init(&t -> donations);
	list_init(&t -> child_list);

	/* Put thread in all_list */
	list_push_front(&all_list, &t -> all_elem);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n" 
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
/* 새로운 프로세스를 스케줄한다. 초입에 인터럽트는 비활성화 되어야한다..
 * 이 함수는 현재 스레드의 상태를 업데이트하고 실행할 다른 스레드를 찾고 스위치를 켠다.
 * 스케줄 함수 내부에서 프린트를 찍어보는건 좋지 않음. */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트를 끈다.
	ASSERT (thread_current()->status == THREAD_RUNNING); // 스레드 상태를 스레드 실행 상태로 갱신한다.
	while (!list_empty (&destruction_req)) { // destruction req(삭제 요청) 리스트의 크기를 체크
		struct thread *victim = // 한명씩 꺼내서
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
			list_remove(&victim-> all_elem);
			palloc_free_page(victim); // 메모리 할당 해제 ( 모가지 날림 )
	}
	thread_current ()->status = status; // 현재 스레드 상태 갱신
	schedule (); // 새로운 스케줄 생성
}

static void
schedule (void) {
	struct thread *curr = running_thread (); // 현재 실행중인 스레드
	struct thread *next = next_thread_to_run (); // 다음 실행할 스레드

	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트 OFF
	ASSERT (curr->status != THREAD_RUNNING); // 스레드 상태 갱신
	ASSERT (is_thread (next)); // 다음 스레드가 있는지 확인
	/* Mark us as running. */
	next->status = THREAD_RUNNING; // 다음 스레드 상태 갱신

	/* Start new time slice. */
	thread_ticks = 0; // 스레드 틱 초기화

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem); // 삭제 리스트에 추가
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next); // 스레드 교체 후 
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

void check_preemption (void) {

	if (list_empty(&ready_list) || thread_current() == idle_thread) {
		return;
	}

	struct list_elem *begin = list_begin(&ready_list);
	struct thread *t = list_entry(begin, struct thread, elem);
	
	// 새로 들어온 스레드의 우선순위가 현재 실행중인 스레드보다 높은 경우 Preemtion(선점)이 발생.
	if (thread_current() -> priority < t -> priority )
		thread_yield();
}

void increase_recent_cpu (void) {
	if ( thread_current() != idle_thread )
		thread_current() -> recent_cpu = ADD_X_N( thread_current() -> recent_cpu, 1); // 매 tick 마다 recent_cpu 크기 추가
}

void calc_priority (struct thread* t) {
	int nice = t -> nice;
	int recent_cpu = t -> recent_cpu;

	t -> priority = CONV_TO_INT_NEAREST(PRI_MAX - DIVIDE_X_N( recent_cpu, 4 )) - ( nice * 2 );
}

void calc_recent_cpu (struct thread* t) {
	int nice = t -> nice;
	int recent_cpu = t -> recent_cpu;

	t -> recent_cpu = ADD_X_N(MULTIPLY(DIVIDE( 2 * load_avg , ADD_X_N( 2 * load_avg, 1 )), recent_cpu), nice);
}

void calc_load_average (void) {
	int ready_list_size = list_size(&ready_list);

	// 현재 스레드가 idle_thread 인 경우
	if ( thread_current() != idle_thread) {
		ready_list_size += 1;
	}

	load_avg = MULTIPLY(DIVIDE_X_N( CONV_TO_FIXED(59), 60 ), load_avg)
						+ MULTIPLY_X_N(DIVIDE_X_N( CONV_TO_FIXED(1), 60 ), ready_list_size);
}

void recalc_priority (void) {
	for (struct list_elem *e = list_begin(&all_list); e != list_end (&all_list); e = list_next(e)) {
		struct thread *nt = list_entry (e, struct thread, all_elem);
		
		calc_priority(nt); // 우선순위 계산
	}

}

void recalc_all (void) {
		calc_load_average(); // load_average 사용량 계산

	for (struct list_elem *e = list_begin(&all_list); e != list_end (&all_list); e = list_next(e)) {
		struct thread *nt = list_entry (e, struct thread, all_elem);
		
		calc_recent_cpu(nt); // CPU 사용량 계산
		calc_priority(nt); // 우선순위 계산
	}
}
