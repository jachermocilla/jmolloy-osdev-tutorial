// sync.c -- A mutex.
//           Written for JamesM's kernel development tutorials.
//           New in chapter 12.

#include "sync.h"
#include "thread.h"

void mutex_init(mutex_t *m)
{
    m->locked  = 0;
    m->waiters = 0;
}

void mutex_lock(mutex_t *m)
{
    u64int f = irq_save();

    // `while`, not `if`. Between the unlock that woke us and the moment we
    // actually get the CPU back, any other thread can walk in and take the
    // lock. Being woken is not being given the lock; it is being told to look
    // again. Every textbook says this and every reader ignores it once.
    while (m->locked)
    {
        wq_enqueue(&m->waiters, (thread_t *)current_thread);
        block();
    }

    m->locked = 1;

    irq_restore(f);
}

void mutex_unlock(mutex_t *m)
{
    u64int f = irq_save();

    m->locked = 0;

    // Hand the lock to nobody -- just make one waiter runnable and let it race
    // for it like everyone else. Handing the lock directly to the woken thread
    // is a different design with different fairness, and it is worth knowing
    // that you chose.
    thread_t *t = wq_dequeue(&m->waiters);
    if (t)
        unblock(t);

    irq_restore(f);
}

// Why is `m->locked = 1` not a `lock xchg`?
//
// Because interrupts are off, and on one CPU that is the whole of mutual
// exclusion: nothing else can be executing. The read of m->locked and the write
// that follows it cannot be separated, because there is no other thread of
// execution to separate them.
//
// That is a fact about the machine you are running on, not about the code you
// wrote. Boot a second core and it stops being true instantly: the other core
// was never interrupted, so `cli` on this one says nothing to it. Both cores
// read locked == 0, both write 1, both proceed. The fix is an instruction that
// makes read-and-write one indivisible bus transaction:
//
//     static inline u32int xchg(volatile u32int *p, u32int v)
//     {
//         asm volatile("lock xchgl %0, %1" : "+r"(v), "+m"(*p) :: "memory");
//         return v;
//     }
//
//     while (xchg(&m->locked, 1) == 1) { ...block... }
//
// This kernel has one CPU, so the simple version is correct here and now. It is
// worth writing it the simple way, seeing that it works, and knowing exactly
// which sentence in this comment stops being true when you add a core.
