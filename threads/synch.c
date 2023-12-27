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

/* ************************ Project 1 ************************ */
static bool dec_pri_function(const struct list_elem *, const struct list_elem *, void *);
static bool dec_pri_in_sema_function (const struct list_elem *, const struct list_elem *, void *);
static bool dec_pri_in_donate_function (const struct list_elem *, const struct list_elem *, void *);
static void remove_waiting_lock(struct lock *);

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


/*
	이 연산은 주어진 세마포어(SEMA)의 값이 양수가 될 때까지 기다린 다음, 값을 원자적으로 감소시킵니다.

	세마포어의 Down 연산은 일반적으로 어떤 리소스의 사용을 시작하거나,
    크리티컬 섹션에 들어가기 전에 호출됩니다. 세마포어의 값이 양수인 경우에만 Down 연산이 성공하며,
	값이 0이거나 음수인 경우에는 해당 세마포어의 값이 양수가 될 때까지 대기합니다.
	이 함수는 슬립(Sleep)이 발생할 수 있으므로, 이 함수는 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
	즉, 이 함수는 인터럽트 컨텍스트에서 호출되지 않아야 합니다. 
	함수가 슬립하지 않으면 인터럽트 비활성화 상태에서 호출할 수 있지만, 
	스레드가 슬립하면 다음으로 스케줄되는 스레드가 아마도 인터럽트를 다시 활성화할 것입니다.

	이러한 Down 연산은 주로 공유된 자원에 대한 접근을 제어하고, 
	여러 스레드 간에 일관성을 유지하기 위해 사용됩니다.
	Down 연산을 호출하면 세마포어의 값을 감소시켜 해당 자원에 대한 사용 권한을 획득하고,
	다른 스레드는 이 세마포어가 다시 양수가 될 때까지 기다리게 됩니다.

	sema_down은 sleep이 발생할 수 있다?
		- 함수가 실행되는 도중에 스레드가 블록되어(waiting 상태가 되어) 잠들 수 있다
		- 세마포어 값이 0인 경우: 세마포어의 값이 0이면 Down 연산이 불가능하므로 스레드는 해당 
			세마포어의 값이 양수가 될 때까지 대기
*/
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
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


/*
	주어진 세마포어(SEMA)의 값을 증가시키고, 만약 세마포어를 기다리는 스레드 중 하나가 있다면 깨움

	세마포어의 Up 연산은 주로 어떤 리소스의 사용이 끝났음을 나타내거나, 
	크리티컬 섹션을 빠져나가기 전에 호출

	세마포어의 값이 증가하면서 기다리고 있는 스레드 중 하나가 깨어나서 리소스에 접근할 수 있게 됨
	이 함수는 인터럽트 핸들러 내에서 호출할 수 있음

	즉, 인터럽트 컨텍스트에서도 안전하게 호출될 수 있음
	이는 일반적으로 세마포어 연산이 인터럽트에서 사용되는 경우가 있기 때문에 중요한 특징
*/
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		list_sort(&sema->waiters, dec_pri_function, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	try_yield();
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

/*
   락을 초기화합니다.
   주어진 락에 대한 초기화 작업을 수행하고,
   해당 락의 보유자(holder)를 NULL로, 세마포어를 1로 초기화합니다.
*/
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
* priority donation inherit - 
	* old_priority라는 변수를 thread에 추가하여 구현해 보았음
	* multiple donate에서 이전상태의 우선순위를 기억하고 있어야하는 문제 발생
	* 즉, donation 받은 정보에 대한 리스트가 필요
*/
void
lock_acquire (struct lock *lock) {
	struct thread *curr = thread_current();
	
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	if (!thread_mlfqs) {
		if (lock->semaphore.value == 0) {
			curr->wait_on_lock = lock;
			if (lock->holder->priority < curr->priority) {
				list_insert_ordered(&lock->holder->donation, &curr->donation_elem, dec_pri_in_donate_function, NULL);
				donate_priority ();
			}
		}
	}
	

	sema_down (&lock->semaphore);

	if (!thread_mlfqs) {
		curr->wait_on_lock = NULL;
	}
	
	lock->holder = curr;
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
	struct thread *curr = thread_current();
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	if (!thread_mlfqs) {
		remove_waiting_lock(lock);

		curr->priority = curr->origin_priority;
		if (!list_empty(&curr->donation)) {
			struct thread *top_thread = list_entry(list_front(&curr->donation),
												struct thread, donation_elem);
			if (top_thread->priority > curr->priority)
				curr->priority = top_thread->priority;
		}
	}
	

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/*
   Condition Variable을 초기화합니다.
   주어진 Condition Variable에 대한 대기 리스트를 초기화합니다.
   
   cond: 초기화할 Condition Variable 구조체의 포인터
*/
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* 
   Condition Variable에서 기다리는 스레드를 표현하는 세마포어 요소를 초기화하고,
   해당 세마포어 요소를 Condition Variable의 대기 리스트에 삽입한 후,
   현재 보유한 락을 해제하고 대기 상태로 들어갑니다.
   이 함수는 반드시 인터럽트가 비활성화된 상태에서 호출되어야 하며,
   현재 스레드가 주어진 락을 보유하고 있어야 합니다.
   
   기다리는 동안 세마포어 요소의 세마포어를 0으로 초기화하고,
   대기 리스트에는 스레드들을 우선순위 기준으로 정렬하여 삽입합니다.
   
   락을 해제하고 대기 상태로 들어간 후, 실제로 깨어나려면 다른 스레드에서
   조건 변수에 signal 또는 broadcast를 호출해야 합니다.
*/
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_insert_ordered (&cond->waiters, &waiter.elem, dec_pri_in_sema_function, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/*
   Condition Variable에서 대기 중인 스레드 중 하나를 깨웁니다.
   대기 중인 스레드가 있을 경우, 대기 리스트를 우선순위 기준으로 정렬하고,
   가장 우선순위가 높은 스레드를 깨워서 대기 상태에서 깨어나게 합니다.
   
   주어진 락이 현재 스레드에 의해 보유되고 있어야 하며,
   인터럽트가 비활성화된 상태에서 호출되어야 합니다.
*/
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		list_sort(&cond->waiters, dec_pri_in_sema_function, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/*
   Condition Variable에서 대기 중인 모든 스레드를 깨웁니다.
   대기 중인 스레드가 있다면, 그 모든 스레드를 깨워서 대기 상태에서 깨어나게 합니다.
   
   주어진 락이 현재 스레드에 의해 보유되고 있어야 하며,
   인터럽트가 비활성화된 상태에서 호출되어야 합니다.
*/
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
/*
   리스트 정렬에서 사용되는 스레드 우선순위에 따른 비교 함수입니다.
   이 함수는 두 리스트 요소 간의 비교를 수행하고, 정렬 순서를 결정합니다.
*/
static bool 
dec_pri_function(const struct list_elem *a, const struct list_elem *b, void *aux) {
    return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}

/*
   세마포어의 대기 리스트에서 스레드 우선순위에 따른 비교 함수입니다.
   이 함수는 두 세마포어 요소의 대기 스레드 중 가장 높은 우선순위를 비교하여 정렬합니다.
*/
static bool 
dec_pri_in_sema_function (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

	return list_entry(list_begin(&sema_a->semaphore.waiters), struct thread, elem)->priority 
	> list_entry(list_begin(&sema_b->semaphore.waiters), struct thread, elem)->priority;
}

static bool 
dec_pri_in_donate_function (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	return list_entry(a, struct thread, donation_elem)->priority > list_entry(b, struct thread, donation_elem)->priority;
}

/*
 * 현재 스레드의 기부 리스트에서 지정된 락을 기다리는 스레드를 제거
*/
static void
remove_waiting_lock(struct lock *lock) {
	struct list *donation = &(thread_current()->donation); 
    struct list_elem *donor_elem;	
    struct thread *donor_thread;

    if (list_empty(donation))
        return;

    donor_elem = list_front(donation);

    while (1)
    {
        donor_thread = list_entry(donor_elem, struct thread, donation_elem);
        if (donor_thread->wait_on_lock == lock)		   
            list_remove(&donor_thread->donation_elem); 
        donor_elem = list_next(donor_elem);
        if (donor_elem == list_end(donation))
            return;
    }
}