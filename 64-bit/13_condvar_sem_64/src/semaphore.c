// semaphore.c -- A counting semaphore.
//                Written for JamesM's kernel development tutorials.
//                New in chapter 13.

#include "semaphore.h"
#include "sync.h"
#include "thread.h"

void sem_init(semaphore_t *s, s64int count)
{
    s->count   = count;
    s->waiters = 0;
}

void sem_wait(semaphore_t *s)
{
    u64int f = irq_save();

    // The same `while`, for the same reason it is a `while` in mutex_lock. A
    // post wakes a waiter; it does not hand it the unit. Between the wake and
    // the moment this thread runs, another thread can call sem_wait and take
    // the count back to zero. So a woken thread re-tests, and only the test
    // decides. Being woken is being told to look again -- one primitive up from
    // the mutex, the rule has not changed.
    while (s->count == 0)
    {
        wq_enqueue(&s->waiters, (thread_t *)current_thread);
        block();
    }

    s->count--;

    irq_restore(f);
}

void sem_post(semaphore_t *s)
{
    u64int f = irq_save();

    s->count++;

    // Wake one, and let it race for the count like everyone else. If the queue
    // is empty the increment simply stands, and the next thread to call
    // sem_wait passes without blocking. That asymmetry is the point of the
    // count: a post that arrives before its wait is not lost, it is remembered.
    thread_t *t = wq_dequeue(&s->waiters);
    if (t)
        unblock(t);

    irq_restore(f);
}

// Why is `s->count--` not an atomic decrement?
//
// For the reason mutex_lock's plain store was not a `lock xchg`: interrupts are
// masked, one CPU, nothing else is executing. The read of the count, the test,
// and the write cannot be split because there is no second thread of execution
// to split them.
//
// A semaphore is where that shortcut is most tempting to forget, because the
// count *looks* like ordinary shared arithmetic and the whole chapter has just
// finished proving that ordinary shared arithmetic races. It does not race here
// only because the masking makes the three steps one -- and that stops being
// true the instant a second core can run sem_wait at the same time. Then the
// count needs `lock xadd`, and this comment is how you will remember why.
