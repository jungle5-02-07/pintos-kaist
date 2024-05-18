/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered(&sema -> waiters, &thread_current() -> elem, compare_priority, NULL); // priority-preempt
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		list_sort(&sema->waiters, compare_priority, NULL); 
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}
	
	sema->value++;
	check_preemption();

	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 
	- If the lock is not avaliable, store address of the lock.
	- Store the current priority and maintain donated threads on list (multiple donation)
	- Doante priority
*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));


	struct thread *holder;
	struct thread *t = thread_current(); // 현재 스레드

	if ( lock -> holder == NULL ) { // 현재 Lock이 없다면 
		lock -> holder = t; //  해당 락의 홀더에 스레드 할당
	}
	else {
		holder = lock -> holder;
		t -> wait_on_lock = lock; // 대기할 Lock을 할당
		list_insert_ordered( &(holder -> donations), &t -> d_elem, compare_priority, NULL ); // 스레드와 연관있는 doantions을 우선순위 순으로 정렬
		holder -> priority = get_donation_priority(holder); // lock의 Holder가 가장 높은 우선순위를 양도 받아 스레드에 저장
		donate_nested_priority (holder);
	}

	t -> priority = get_donation_priority(t); // 현재 스레드의 우선순위 갱신
	donate_nested_priority (t);



	sema_down (&lock->semaphore);
	lock->holder = thread_current();
	t -> wait_on_lock = NULL;
	
	// sema_down은 해당 락이 사용가능할 때까지 스레드를 대기 상태로 만든다.
	// 그래서 sema_down 이후 코드에 대해서는 해당 스레드가 락을 소유한 상태로 간주해도 되기 때문에
	// sema_down 이후에 한번더 명시적으로 lock을 설정해야한다.
}

void donate_nested_priority(struct thread *t) {
	struct thread *nt;
	// struct thread *t = thread_current;
	int root_priority = t -> original_priority;

	while ( t -> wait_on_lock != NULL ) {
		nt = t -> wait_on_lock -> holder;

		if ( nt -> priority < root_priority ) {
			nt -> priority = root_priority;
		}

		t = nt;
	}
}


/* 우선순위 양도를 고려하여 가장 높은 우선순위를 return */
int get_donation_priority(struct thread *t) {
	// struct list_elem *e = list_max(t -> donations, check_priority, NULL);
	// struct thread *dpt = list_entry(e, struct thread, elem);

	// return dpt -> priority;

	if (list_empty(&(t -> donations)))  {
		return t -> original_priority;
	}// donation이 비어있을 경우에 대한 방어 코드

	
	struct list_elem *e = list_front(&(t -> donations)); // 도네이션의 첫번째 요소 get
	struct thread *dt = list_entry(e, struct thread, d_elem); // elem을 통해서 스레드 생성

	return t -> original_priority < dt -> priority ? dt -> priority : t -> original_priority; // 우선순위 원본과 donations의 최대 우선순위를 비교해서 할당
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	struct thread *t = lock -> holder;
	// t -> priority = t -> original_priority; // release 될 Lock의 Holder Thread의 우선순위를 초기화.

// holder의 donation에서 M을 빼내기
	for (struct list_elem *e = list_begin (&(t -> donations)); e != list_end (&(t -> donations)); e = list_next(e)) {
		
		struct thread *nt = list_entry (e, struct thread, d_elem);
		
		if ( nt -> wait_on_lock == lock ) {
			list_remove(e); // lock과 관련된 donation thread를 donation list에서 제거
			// e = ist_remove(e);
			// struct thread *target_t = list_entry (e, struct thread, d_elem);
			// nt -> wait_on_lock = NULL;// M의 wait_on-lock을 NULL로 변경
			// break; -> 빼야됨l
		}
	}

	list_sort(&(t->donations), compare_priority, NULL);

	t -> priority = get_donation_priority(t); // 기존 Lock Holder에게 새로운 우선 순위 양도
	donate_nested_priority (t);



	// Lock의 waiters에서 가장 큰 priority를 가진 스레드를 donation에 연결
	// if (!list_empty(&(lock -> semaphore.waiters))) {
	// 	struct list_elem *t_elem = list_pop_front(&(lock -> semaphore.waiters));
	// 	list_insert_ordered(&nt -> donations, t_elem, compare_priority, NULL); // priority-preempt
	// 	target_t -> priority = get_donation_priority(nt); // 우선 순위 갱신
	// }

	lock->holder = NULL;
	sema_up (&lock->semaphore); // 수정 전에 semaphore value 수정
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem , &semaphore_elem_less, NULL); // priority-condvar

	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
    list_sort (&cond->waiters, &semaphore_elem_less, NULL); // priority-condvar, sema_up 하기 전에 priority donation이 발생할 수 있기 때문에 정렬로 우선순위에 따른 정렬
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

bool semaphore_elem_less(struct list_elem *a, struct list_elem *b, void *aux) {
    struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

		struct list_elem *la = list_begin(&(sa->semaphore.waiters));
		struct list_elem *lb = list_begin(&(sb->semaphore.waiters));

		struct thread *a_thread = list_entry(la, struct thread, elem);
		struct thread *b_thread = list_entry(lb, struct thread, elem);

		int priority_a = a_thread -> priority;
		int priority_b = b_thread-> priority;

		return priority_a > priority_b;
}
